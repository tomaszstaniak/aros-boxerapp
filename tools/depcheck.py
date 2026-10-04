#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Exit 0 if an object is current with respect to its GCC -MMD dependency file.

Current means: the object and its .d exist, every prerequisite listed in the
.d exists, and none is newer than the object. A missing prerequisite (a
deleted or moved header) forces a rebuild instead of being ignored.
Make-syntax escapes are honoured: "\\ " is a space in a path, "$$" a dollar,
and a backslash before a newline continues the line.
Usage: depcheck.py <object>
"""
import os, sys

def prerequisites(text):
    text = text.replace("\\\n", " ")
    rules = []
    for line in text.splitlines():
        if ":" not in line:
            continue
        # The target is everything up to the first unescaped ": ".
        i = 0
        while True:
            i = line.find(":", i)
            if i < 0 or i + 1 >= len(line) or line[i + 1] in " \t":
                break
            i += 1
        if i < 0:
            continue
        rules.append(line[i + 1:])
    for body in rules:
        token, j = "", 0
        while j < len(body):
            c = body[j]
            if c == "\\" and j + 1 < len(body) and body[j + 1] == " ":
                token += " "; j += 2; continue
            if c == "$" and j + 1 < len(body) and body[j + 1] == "$":
                token += "$"; j += 2; continue
            if c in " \t":
                if token:
                    yield token
                token = ""
            else:
                token += c
            j += 1
        if token:
            yield token

if len(sys.argv) != 2:
    sys.exit(__doc__)
obj = sys.argv[1]
dep = obj + ".d"
if not (os.path.isfile(obj) and os.path.isfile(dep)):
    sys.exit(1)
stamp = os.stat(obj).st_mtime_ns
for path in prerequisites(open(dep, encoding="utf-8", errors="surrogateescape").read()):
    try:
        if os.stat(path).st_mtime_ns > stamp:
            sys.exit(1)
    except FileNotFoundError:
        sys.exit(1)
sys.exit(0)
