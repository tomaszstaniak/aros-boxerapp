#!/usr/bin/env python3
"""Tests for scripts/core-provenance.sh, the record build-core.sh writes and
make-package.sh compares: python3 tests/build-tooling/test_core_provenance.py"""
import json, os, shutil, subprocess, tempfile, unittest

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts", "core-provenance.sh")

class CoreProvenance(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="core prov ")
        self.pd = os.path.join(self.root, "patches", "boxer")
        os.makedirs(self.pd)
        self.pin("a" * 40)
        for n in ("0001-a.patch", "0002-b.patch"):
            open(os.path.join(self.pd, n), "w").write("patch " + n + "\n")
        self.series("0001-a.patch", "0002-b.patch")

    def tearDown(self):
        shutil.rmtree(self.root)

    def pin(self, c):
        json.dump({"repositories": {"boxer": {"url": "u", "commit": c}}},
                  open(os.path.join(self.root, "upstreams.json"), "w"))

    def series(self, *names):
        open(os.path.join(self.pd, "series"), "w").write("# comment\n" + "\n".join(names) + "\n")

    def record(self):
        r = subprocess.run(["bash", TOOL, self.root], capture_output=True, text=True)
        return r.returncode, r.stdout

    def test_unchanged_is_identical(self):
        self.assertEqual(self.record(), self.record())
        rc, out = self.record()
        self.assertEqual(rc, 0)
        self.assertIn("upstream pin: " + "a" * 40, out)
        self.assertLess(out.index("0001-a.patch"), out.index("0002-b.patch"))

    def test_patch_content_change_differs(self):
        before = self.record()
        open(os.path.join(self.pd, "0002-b.patch"), "a").write("one more line\n")
        self.assertNotEqual(before, self.record())

    def test_order_change_differs(self):
        before = self.record()
        self.series("0002-b.patch", "0001-a.patch")
        self.assertNotEqual(before, self.record())

    def test_pin_change_differs(self):
        before = self.record()
        self.pin("b" * 40)
        self.assertNotEqual(before, self.record())

    def test_missing_patch_fails(self):
        self.series("0001-a.patch", "0003-gone.patch")
        self.assertNotEqual(self.record()[0], 0)

if __name__ == "__main__":
    unittest.main()
