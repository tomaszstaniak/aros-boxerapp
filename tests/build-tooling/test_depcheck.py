#!/usr/bin/env python3
"""Tests for tools/depcheck.py: run with python3 tests/build-tooling/test_depcheck.py"""
import os, subprocess, sys, tempfile, time, unittest

TOOL = os.path.join(os.path.dirname(__file__), "..", "..", "tools", "depcheck.py")

def run(obj):
    return subprocess.run([sys.executable, TOOL, obj]).returncode

class DepCheck(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="dep check ")
        self.obj = os.path.join(self.dir, "a b.o")
        self.src = os.path.join(self.dir, "src dir", "a b.cpp")
        self.hdr = os.path.join(self.dir, "inc $x", "h.h")
        for p in (self.src, self.hdr):
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "w").write("x")
        esc = lambda p: p.replace("$", "$$").replace(" ", "\\ ")
        open(self.obj + ".d", "w").write(
            f"{esc(self.obj)}: {esc(self.src)} \\\n {esc(self.hdr)}\n")
        old = time.time() - 100
        os.utime(self.src, (old, old)); os.utime(self.hdr, (old, old))
        open(self.obj, "w").write("o")

    def test_current(self):
        self.assertEqual(run(self.obj), 0)

    def test_newer_header_with_space_and_dollar(self):
        os.utime(self.hdr, None)
        time.sleep(0.01); os.utime(self.hdr, (time.time() + 5, time.time() + 5))
        self.assertEqual(run(self.obj), 1)

    def test_missing_prerequisite_rebuilds(self):
        os.remove(self.hdr)
        self.assertEqual(run(self.obj), 1)

    def test_missing_depfile_rebuilds(self):
        os.remove(self.obj + ".d")
        self.assertEqual(run(self.obj), 1)

    def test_missing_object_rebuilds(self):
        os.remove(self.obj)
        self.assertEqual(run(self.obj), 1)

if __name__ == "__main__":
    unittest.main()
