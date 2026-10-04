#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Convert an Apple .strings file into the message table boxer::Emulator reads.

Output: pairs of NUL-terminated key and value, in file order. Values are
encoded as CP437 because DOSBox writes them straight to the DOS screen, as
Boxer did (BXDisplayStringEncoding). Handles the subset of the .strings
syntax the original DOSBox.strings uses: /* */ and // comments, "key" = "value";
with \\" \\\\ \\n \\t \\r escapes. Anything else is an error, not a skip.
Usage: strings2table.py <in.strings> <out.table>
"""
import re, sys

if len(sys.argv) != 3:
    sys.exit(__doc__)
raw = open(sys.argv[1], "rb").read()
for enc in ("utf-16", "utf-8"):
    try:
        text = raw.decode(enc)
        break
    except UnicodeDecodeError:
        continue
else:
    sys.exit("cannot decode " + sys.argv[1])
text = text.lstrip("﻿")

pos, pairs = 0, []
token = re.compile(r'\s+|/\*.*?\*/|//[^\n]*|"((?:[^"\\]|\\.)*)"\s*=\s*"((?:[^"\\]|\\.)*)"\s*;', re.S)
escapes = {'"': '"', "\\": "\\", "n": "\n", "t": "\t", "r": "\r"}

def unescape(s):
    def rep(m):
        c = m.group(1)
        if c not in escapes:
            raise ValueError("unsupported escape \\" + c)
        return escapes[c]
    return re.sub(r"\\(.)", rep, s)

while pos < len(text):
    m = token.match(text, pos)
    if not m:
        sys.exit(f"parse error at offset {pos}: {text[pos:pos+40]!r}")
    if m.group(1) is not None:
        pairs.append((unescape(m.group(1)), unescape(m.group(2))))
    pos = m.end()

with open(sys.argv[2], "wb") as out:
    for k, v in pairs:
        out.write(k.encode("cp437") + b"\0" + v.encode("cp437", errors="strict") + b"\0")
print(f"{len(pairs)} messages -> {sys.argv[2]}")
