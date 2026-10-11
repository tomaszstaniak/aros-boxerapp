#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Merge input manifests into the "build inputs" list of BUILDINFO.txt.

Each input is a file of "<sha256>  <path>" lines, as written by the tools
that read the files (tools/png2inc.py --inputs, tools/make-fonts.py
--inputs, tools/depmanifest.py --only). Paths inside the project root are
printed relative to it, others (an AROS ISO's Fonts/) as given. Each file
is printed once, sorted. Relative paths in an input are relative to the
project root. The hash a tool recorded must still be the file's
hash now: a file that changed while the build ran is not a build input
anyone can name afterwards.
Exit 2, printing nothing, when an input is missing, empty or malformed, a
listed file is missing or differs from its recorded hash, or one path is
recorded with two hashes.
Usage: inputmanifest.py <project-root> <manifest>...
"""
import hashlib, os, re, sys

LINE = re.compile(r"^([0-9a-f]{64})  (\S.*)$")


def fail(msg):
    sys.stderr.write("inputmanifest: %s\n" % msg)
    sys.exit(2)


def main(argv):
    if len(argv) < 2:
        fail("usage: inputmanifest.py <project-root> <manifest>...")
    root = os.path.realpath(argv[0])
    rows = {}
    for manifest in argv[1:]:
        try:
            lines = open(manifest, encoding="utf-8", errors="surrogateescape").read().splitlines()
        except OSError as e:
            fail("cannot read %s: %s" % (manifest, e))
        if not lines:
            fail("%s is empty" % manifest)
        for n, line in enumerate(lines, 1):
            m = LINE.match(line)
            if not m:
                fail("%s:%d: malformed line %r" % (manifest, n, line))
            h, path = m.groups()
            # depmanifest.py writes paths relative to the project root.
            real = os.path.realpath(os.path.join(root, path))
            if not os.path.isfile(real):
                fail("%s lists %s, which does not exist" % (manifest, path))
            if real == root or real.startswith(root + os.sep):
                name = os.path.relpath(real, root)
            else:
                name = path
            if rows.get(name, h) != h:
                fail("%s is recorded with two hashes" % name)
            now = hashlib.sha256(open(real, "rb").read()).hexdigest()
            if now != h:
                fail("%s changed while the build ran (recorded %s, now %s)" % (name, h, now))
            rows[name] = h
    for name in sorted(rows):
        print("%s  %s" % (rows[name], name))


if __name__ == "__main__":
    main(sys.argv[1:])
