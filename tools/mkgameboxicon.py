#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Write a Workbench icon for a Boxer gamebox (decision D1). The gamebox
itself is never touched except where --layout inside says so; the icon is
an addition and never carries format data.

Layouts (each was tried in Wanderer before it was adopted):
  sidecar  "<dir>/<Name>.info" beside "<Name>.boxer", a lone project icon:
           Wanderer double-click starts the default tool with the argument
           (<dir>, "<Name>"); BoxerUI maps it to "<Name>.boxer". Default.
  drawer   "<Name>.boxer.info", the drawer's own icon. Wanderer opens a
           directory as a drawer whatever the icon type; WBRun/
           OpenWorkbenchObject honour the type and start the default tool.
  inside   "<Name>.boxer/<label>.info", a lone launcher icon in the drawer.

Usage: mkgameboxicon.py GAMEBOX.boxer [--layout sidecar|drawer|inside]
         [--type project|tool] [--default-tool PATH] [--allow-device-path] [--label NAME]
         [--image PNG] [--stack N] [--tooltype K=V]... [--out FILE]

File layout: classic DiskObject (workbench/workbench.h, do_Magic 0xE310)
with an explicit do_Type and a 2-plane fallback image, followed by an
OS 3.5 FORM ICON with FACE + one uncompressed 8-bit IMAG (palette), which
mainline and ABIv11 icon.library read identically (diskobj35io.c,
ReadImage35, ImageFormat 0). The ARGB chunk is avoided on purpose: the two
ABIs disagree on its compressed-size field.
"""
import argparse, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from png2inc import decode  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WBTOOL, WBPROJECT = 3, 4


def quantize(w, h, rgba):
    """Index 0 = transparent; at most 255 opaque colours, reducing bit depth
    until they fit."""
    for bits in range(8, 0, -1):
        mask = (0xFF << (8 - bits)) & 0xFF
        pal, idx = [], []
        lut = {}
        for i in range(w * h):
            r, g, b, a = rgba[i * 4:i * 4 + 4]
            if a < 128:
                idx.append(0)
                continue
            key = (r & mask, g & mask, b & mask)
            if key not in lut:
                lut[key] = len(pal) + 1
                pal.append(key)
            idx.append(lut[key])
        if len(pal) <= 255:
            return [(0, 0, 0)] + pal, idx
    raise SystemExit("cannot quantize")


def chunk(cid, body):
    return cid + struct.pack(">I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def build(kind, default_tool, tooltypes, stack, png, pixels=None, drawer=False):
    w, h, rgba = pixels if pixels else decode(png)
    pal, idx = quantize(w, h, rgba)
    # classic fallback image, 2 planes: 0 background, 1 dark, 2 light, 3 mid
    words = (w + 15) // 16
    pens = []
    for i in range(w * h):
        r, g, b, a = rgba[i * 4:i * 4 + 4]
        lum = (r * 3 + g * 6 + b) // 10
        pens.append(0 if a < 128 else (1 if lum < 85 else (2 if lum > 170 else 3)))
    planes = b""
    for plane in range(2):
        for y in range(h):
            row = bytearray(words * 2)
            for x in range(w):
                if pens[y * w + x] & (1 << plane):
                    row[x // 8] |= 0x80 >> (x % 8)
            planes += bytes(row)
    gadget = struct.pack(">IhhhhHHHIIIiIHI", 0, 0, 0, w, h, 0x0004, 0x0003, 0x0001,
                         1, 0, 0, 0, 0, 0, 1)
    obj = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIiiIIi", kind, 0, 1 if default_tool else 0, 1 if tooltypes else 0,
        -0x80000000, -0x80000000, 1 if drawer else 0, 0, stack)
    assert len(obj) == 78
    if drawer:
        # OldDrawerData: NewWindow (left, top, width, height, pens, IDCMP,
        # flags, 5 ignored pointers, min/max size, type WBENCHSCREEN), then
        # dd_CurrentX/Y. Wanderer sizes and places the window itself.
        obj += struct.pack(">hhhhBBII20xhhHHHii", 50, 50, 400, 200, 0xFF, 0xFF, 0, 0,
                           90, 40, 0xFFFF, 0xFFFF, 1, 0, 0)
        assert len(obj) == 78 + 56
    out = obj + struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 3, 0, 0) + planes

    def s(text):
        raw = text.encode("latin-1") + b"\0"
        return struct.pack(">I", len(raw)) + raw
    if default_tool:
        out += s(default_tool)
    if tooltypes:
        out += struct.pack(">I", (len(tooltypes) + 1) * 4) + b"".join(s(t) for t in tooltypes)
    if drawer:
        out += struct.pack(">IH", 0, 0)   # NewDrawerData: dd_Flags, dd_ViewModes default
    n = len(pal)
    face = struct.pack(">BBBBH", w - 1, h - 1, 0, 0x11, n * 3 - 1)
    imag = struct.pack(">BBBBBBHH", 0, n - 1, 0x03, 0, 0, 8, w * h - 1, n * 3 - 1)
    imag += bytes(idx) + b"".join(bytes(c) for c in pal)
    form = b"ICON" + chunk(b"FACE", face) + chunk(b"IMAG", imag)
    return out + b"FORM" + struct.pack(">I", len(form)) + form, n


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("gamebox")
    p.add_argument("--layout", choices=["sidecar", "drawer", "inside"], default="sidecar")
    p.add_argument("--type", choices=["project", "tool"], default="project")
    # The icon travels with the gamebox, so its default tool must not name
    # one machine's install drawer. "Boxer:" is the assign the installer
    # makes for BoxerUI's drawer. Workbench (openworkbenchobjecta.c) first
    # Locks the default tool as given - relative to Wanderer's own current
    # directory, not to the icon - and only for a name without ':' or '/'
    # searches Wanderer's command path. An assign is the one form that
    # works wherever the icon is.
    p.add_argument("--default-tool", default="Boxer:BoxerUI")
    p.add_argument("--allow-device-path", action="store_true",
                   help="accept a default tool on a volume or device (tests only)")
    p.add_argument("--label", default="Play")
    p.add_argument("--image", default=os.path.join(ROOT, "assets/runtime/boxer/boxer-32.png"))
    p.add_argument("--stack", type=int, default=65536)
    p.add_argument("--tooltype", action="append", default=[])
    p.add_argument("--out", help="write here instead of the layout's path (host staging)")
    a = p.parse_args()
    gb = a.gamebox.rstrip("/")
    dev = a.default_tool.split(":", 1)[0] if ":" in a.default_tool else ""
    if dev and dev.upper() != "BOXER" and not a.allow_device_path:
        raise SystemExit("default tool %r names a volume or device; use Boxer:BoxerUI "
                         "(or --allow-device-path for a test)" % a.default_tool)
    if not gb.lower().endswith(".boxer"):
        raise SystemExit("not a gamebox name (*.boxer): %s" % gb)
    if a.out:
        out = a.out
    elif a.layout == "drawer":
        out = gb + ".info"
    elif a.layout == "sidecar":
        out = gb[:-6] + ".info"
        if os.path.exists(gb[:-6]):
            raise SystemExit("sidecar needs a free name: %s exists" % gb[:-6])
    else:
        out = os.path.join(gb, a.label + ".info")
    data, n = build(WBPROJECT if a.type == "project" else WBTOOL,
                    a.default_tool, a.tooltype, a.stack, a.image)
    open(out, "wb").write(data)
    print("%s: %d bytes, %s %s, default tool %r, %d colours" % (out, len(data), a.layout, a.type, a.default_tool, n))


if __name__ == "__main__":
    main()
