#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Check the private C++ runtime of the Raspberry Pi target against its pins.

The pin file (third_party/llvm-runtimes-aros/PINS) has one entry per line,
"<kind> <sha256> <name>", kind tarball, diff, archive or headers; '#' starts
a comment. The headers pin is the sha256 of the header manifest
(HEADERS.sha256 in the runtime directory: "<sha256>  <path>" for every file
below include/, sorted by path), which build-cxx-runtime.sh writes.

  cxxrt_pins.py tarball <pins> <file>          the LLVM tarball
  cxxrt_pins.py diffs <pins> <dir>             every pinned diff, in <dir>
  cxxrt_pins.py archives <pins> <libdir>       every pinned archive, in <libdir>
  cxxrt_pins.py manifest <rtdir>               print the header manifest of <rtdir>/include
  cxxrt_pins.py headers <pins> <rtdir>         <rtdir>/HEADERS.sha256 is the pinned
                                               manifest, and <rtdir>/include holds
                                               exactly the files it lists (a changed,
                                               added or removed header is reported)
  cxxrt_pins.py record <pins> <file> <prefix>  lines "<prefix><kind> <sha> <name>"
                                               in <file> name exactly the pins
  cxxrt_pins.py lines <pins>                   print the pins (for BUILDINFO)

Exit 0 when everything matches, 1 on a mismatch (each named on stderr), 2 on
a usage or format error.
"""
import hashlib, os, re, sys

KINDS = ("tarball", "diff", "archive", "headers")
LINE = re.compile(r"^(tarball|diff|archive|headers) ([0-9a-f]{64}) (\S+)$")
MANIFEST = "HEADERS.sha256"


def fail(msg, code=2):
    print("cxxrt_pins: " + msg, file=sys.stderr)
    sys.exit(code)


def load(path):
    pins = []
    try:
        with open(path) as f:
            text = f.read()
    except OSError as e:
        fail("cannot read %s: %s" % (path, e))
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        m = LINE.match(line)
        if not m:
            fail("%s:%d: malformed pin: %r" % (path, n, raw))
        pins.append(m.groups())
    for k in KINDS:
        if not any(p[0] == k for p in pins):
            fail("%s: no %s pin" % (path, k))
    names = [(k, n) for k, _, n in pins]
    if len(set(names)) != len(names):
        fail("%s: duplicate pin" % path)
    return pins


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def check_files(pins, kind, where, single=None):
    bad = 0
    for k, want, name in pins:
        if k != kind:
            continue
        p = single if single else os.path.join(where, name)
        if not os.path.isfile(p):
            print("MISSING  %s %s (%s)" % (kind, name, p), file=sys.stderr)
            bad = 1
            continue
        got = sha(p)
        if got != want:
            print("MISMATCH %s %s: sha256 %s, pinned %s" % (kind, name, got, want), file=sys.stderr)
            bad = 1
    return bad


def manifest(rtdir):
    """The header manifest of <rtdir>/include: "<sha256>  <path>" per file,
    paths relative to <rtdir>, sorted. Anything but files and directories
    (a symbolic link, for example) is an error: it could point anywhere."""
    top = os.path.join(rtdir, "include")
    if not os.path.isdir(top):
        fail("no include directory: %s" % top)
    lines = []
    for d, dirs, files in os.walk(top):
        for n in dirs + files:
            p = os.path.join(d, n)
            if os.path.islink(p) or not (os.path.isdir(p) or os.path.isfile(p)):
                fail("not a plain file or directory: %s" % p)
        for n in files:
            p = os.path.join(d, n)
            lines.append((os.path.relpath(p, rtdir).replace(os.sep, "/"), sha(p)))
    lines.sort()
    return "".join("%s  %s\n" % (h, rel) for rel, h in lines)


def check_headers(pins, rtdir):
    bad = 0
    mf = os.path.join(rtdir, MANIFEST)
    if not os.path.isfile(mf):
        print("MISSING  headers %s (%s)" % (MANIFEST, mf), file=sys.stderr)
        return 1
    for k, want, name in pins:
        if k == "headers" and sha(mf) != want:
            print("MISMATCH headers %s: sha256 %s, pinned %s" % (name, sha(mf), want), file=sys.stderr)
            bad = 1
    listed = {}
    with open(mf) as f:
        for raw in f.read().splitlines():
            m = re.match(r"^([0-9a-f]{64})  (\S.*)$", raw)
            if not m:
                fail("%s: malformed line: %r" % (mf, raw))
            listed[m.group(2)] = m.group(1)
    found = {}
    for raw in manifest(rtdir).splitlines():
        h, rel = raw.split("  ", 1)
        found[rel] = h
    for rel in sorted(set(listed) | set(found)):
        if rel not in found:
            print("REMOVED  header %s" % rel, file=sys.stderr)
            bad = 1
        elif rel not in listed:
            print("ADDED    header %s" % rel, file=sys.stderr)
            bad = 1
        elif found[rel] != listed[rel]:
            print("CHANGED  header %s" % rel, file=sys.stderr)
            bad = 1
    return bad


def main(argv):
    if len(argv) < 3:
        fail(__doc__.strip().split("\n\n")[1])
    if argv[1] == "manifest" and len(argv) == 3:
        sys.stdout.write(manifest(argv[2]))
        return 0
    cmd, pins = argv[1], load(argv[2])
    if cmd == "lines" and len(argv) == 3:
        for p in pins:
            print("%s %s %s" % p)
        return 0
    if cmd == "tarball" and len(argv) == 4:
        return check_files(pins, "tarball", None, single=argv[3])
    if cmd == "diffs" and len(argv) == 4:
        return check_files(pins, "diff", argv[3])
    if cmd == "archives" and len(argv) == 4:
        return check_files(pins, "archive", argv[3])
    if cmd == "headers" and len(argv) == 4:
        return check_headers(pins, argv[3])
    if cmd == "record" and len(argv) == 5:
        path, prefix = argv[3], argv[4]
        try:
            with open(path) as f:
                text = f.read()
        except OSError as e:
            fail("cannot read %s: %s" % (path, e))
        got = set()
        for raw in text.splitlines():
            if raw.startswith(prefix):
                m = LINE.match(raw[len(prefix):].strip())
                if m:
                    got.add(m.groups())
        want = set(pins)
        for p in sorted(want - got):
            print("NOT RECORDED %s %s %s in %s" % (p + (path,)), file=sys.stderr)
        for p in sorted(got - want):
            print("NOT PINNED   %s %s %s in %s" % (p + (path,)), file=sys.stderr)
        return 0 if got == want else 1
    fail("unknown command or wrong arguments: %s" % " ".join(argv[1:]))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
