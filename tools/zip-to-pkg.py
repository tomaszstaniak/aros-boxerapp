#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""The same package as an AROS .pkg, for systems without UnZip.

    tools/zip-to-pkg.py <dir>/Boxer-<ver>-<target>.zip

Writes the .pkg next to the ZIP. UnZip and lha are not on every mainline AROS
(they come from contrib builds); C:Unpack is, and reads .pkg: "PKG", version 1, a LONG size (of the whole uncompressed
stream), then per file a LONG path length, the path with its NUL, a LONG data
length and the data (big endian), all bzip2-compressed (AROS
workbench/c/Unpack/package.c, tools/package/FORMAT). Unpack makes drawers as
it needs them and keeps no protection bits or dates.
"""
import bz2
import os
import struct
import sys
import zipfile

src = sys.argv[1]
if not src.endswith(".zip"):
    sys.exit("give the package .zip")
out = src[:-4] + ".pkg"
if os.path.exists(out):
    sys.exit(f"{out} exists")
body = bytearray(b"PKG" + bytes([1]) + struct.pack(">I", 0))
n = 0
with zipfile.ZipFile(src) as z:
    for info in z.infolist():
        if info.is_dir():
            continue
        path = info.filename.encode("iso-8859-1")
        data = z.read(info)
        body += struct.pack(">I", len(path)) + path + b"\0" + struct.pack(">I", len(data)) + data
        n += 1
# Unpack checks this size since mainline 5ac123c062 and refuses 0.
body[4:8] = struct.pack(">I", len(body))
with open(out, "wb") as f:
    f.write(bz2.compress(bytes(body), 9))
print(f"{out}: {n} files, {os.path.getsize(out)} bytes")
