#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Make BoxerUI's private outline font descriptions: copies of the system's
"Vera Sans.otag" / "Vera Sans Bold.otag" with the freetype2 engine's metric
source (OT_Spec4_Metric) set to the font's global bounding box.

Why: the system descriptions use metric source 3 (OS/2 typo ascender and
descender, 1556/-492 units for Vera). The freetype2 engine scales the font
so that this range fills the requested YSize (workbench/libs/freetype2/
glyph.c, SetInstance); Vera's real ink reaches 1901/-483 (head bbox), so
glyphs stick out of the cell. diskfont then puts the baseline at the
largest glyph ascent (bullet.c, OTAG_GetGlyphMaps) and cuts every glyph to
the YSize rows (OTAG_MakeCharData): on mainline v1 Vera Sans at YSize 11
gets baseline 9 and loses the lower rows of its descenders. With the
bounding box as the metric source the cell holds every glyph: YSize equals
(1901 + 483) / 2048 = 1.164 em, so BoxerUI asks for YSize round(pt * 1.164)
to get glyphs of pt pixels per em.

The copies point at the same TrueType files (Fonts:TrueType/...), so no
font data is shipped. Source: the AROS ISO's Fonts/ directory; the two
.otag files are byte-identical on the ABIv11 2026.09 and the mainline
2026-09-22 ISOs (their sha1 is checked below).

The family name (OT_Family) is rewritten from "Bitstream Vera Sans" to
"BoxerSans": the descriptions are modified files from AROS's
workbench/fonts/truetype/bitstream directory, which is under the Bitstream
Vera licence (LICENSES/Bitstream-Vera.txt), and that licence requires
modified Font Software to carry a name without "Bitstream" or "Vera". The
string is overwritten in place and padded with NULs, so no offset moves.
OT_Spec1_FontFile still names Fonts:TrueType/VeraSans*.ttf: that is the
path of the unmodified system font the description points to, not a name
of this file. The licence text must ship next to the generated files.

Usage: make-fonts.py <iso Fonts dir> <output dir>
"""
import hashlib, os, struct, sys

SOURCES = {
    "BoxerSans": ("Vera Sans.otag", "fe5394c16195188d85c0b6305b8a680d602845cf"),
    "BoxerSansBold": ("Vera Sans Bold.otag", "980d05d092030984d504a40ed122730a5ec9ad5c"),
}
OT_Spec4_Metric = 0x80001104   # freetype2 engine: metric source
METRIC_GLOBALBBOX = 0
OT_Family = 0x80009003         # indirect string: font family name
NEW_FAMILY = b"BoxerSans"


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for name, (otag, sha1) in SOURCES.items():
        data = bytearray(open(os.path.join(src, otag), "rb").read())
        if hashlib.sha1(data).hexdigest() != sha1:
            raise SystemExit("%s: unexpected content (sha1 %s)" % (otag, hashlib.sha1(data).hexdigest()))
        for off in range(0, len(data) - 8, 8):
            tag, value = struct.unpack(">II", data[off:off + 8])
            if tag == OT_Spec4_Metric:
                if value != 3:
                    raise SystemExit("%s: metric source %d, expected 3" % (otag, value))
                data[off + 4:off + 8] = struct.pack(">I", METRIC_GLOBALBBOX)
                break
            if tag == 0:   # TAG_DONE: the metric tag is missing
                raise SystemExit("%s: no OT_Spec4_Metric tag" % otag)
        for off in range(0, len(data) - 8, 8):
            tag, value = struct.unpack(">II", data[off:off + 8])
            if tag == OT_Family:
                end = data.index(b"\0", value)
                if data[value:end] != b"Bitstream Vera Sans" or len(NEW_FAMILY) > end - value:
                    raise SystemExit("%s: unexpected family %r" % (otag, bytes(data[value:end])))
                data[value:end] = NEW_FAMILY.ljust(end - value, b"\0")
                break
            if tag == 0:
                raise SystemExit("%s: no OT_Family tag" % otag)
        open(os.path.join(out, name + ".otag"), "wb").write(data)
        # An outline font's .font file is only the OFCH_ID header.
        open(os.path.join(out, name + ".font"), "wb").write(b"\x0f\x03\x00\x00")
        print("%s.otag sha256 %s" % (name, hashlib.sha256(data).hexdigest()))


main()
