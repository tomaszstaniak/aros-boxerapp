#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Print the source manifest of a build from its GCC -MMD dependency files.

Every file a compile read (sources and all headers, as listed in each
object's <object>.d) that lies inside the project root is printed as
"<sha256>  <path relative to the root>", sorted, once. Files below an
--exclude directory (generated output whose identity is recorded
separately) and files outside the root (SDK, toolchain) are left out.
Exit 2, printing nothing, when an object has no .d, a listed file is
missing, or nothing would be listed: a manifest must never be quietly
incomplete.
Usage: depmanifest.py <project-root> [--exclude <dir>]... <object>...
"""
import hashlib, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from depcheck import prerequisites  # noqa: E402

def fail(msg):
    sys.stderr.write("depmanifest: %s\n" % msg)
    sys.exit(2)

def inside(path, d):
    return path == d or path.startswith(d.rstrip(os.sep) + os.sep)

def main(argv):
    if len(argv) < 2:
        fail("usage: depmanifest.py <project-root> [--exclude <dir>]... <object>...")
    root = os.path.realpath(argv[0])
    excludes, objects, i = [], [], 1
    while i < len(argv):
        if argv[i] == "--exclude" and i + 1 < len(argv):
            excludes.append(os.path.realpath(argv[i + 1])); i += 2
        else:
            objects.append(argv[i]); i += 1
    if not objects:
        fail("no objects given")
    files = set()
    for obj in objects:
        dep = obj + ".d"
        try:
            text = open(dep, encoding="utf-8", errors="surrogateescape").read()
        except OSError as e:
            fail("cannot read %s: %s" % (dep, e))
        found = False
        for p in prerequisites(text):
            found = True
            # A relative name is relative to the compile's working
            # directory, which is the caller's.
            real = os.path.realpath(p)
            if not os.path.isfile(real):
                fail("%s lists %s, which does not exist" % (dep, p))
            if not inside(real, root) or any(inside(real, x) for x in excludes):
                continue
            files.add(os.path.relpath(real, root))
        if not found:
            fail("%s lists no files" % dep)
    if not files:
        fail("no project files in the dependencies")
    for rel in sorted(files):
        h = hashlib.sha256(open(os.path.join(root, rel), "rb").read()).hexdigest()
        print("%s  %s" % (h, rel))

if __name__ == "__main__":
    main(sys.argv[1:])
