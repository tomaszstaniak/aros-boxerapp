#!/usr/bin/env python3
"""Tests for scripts/core-provenance.sh, the record build-core.sh writes, and
scripts/check-core-provenance.sh, which make-package.sh uses to compare it:
python3 tests/build-tooling/test_core_provenance.py
CHECK_CORE=<script> runs the comparison cases against another implementation."""
import json, os, shutil, subprocess, tempfile, unittest

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts", "core-provenance.sh")
CHECK = os.environ.get("CHECK_CORE") or os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts", "check-core-provenance.sh")

class Fixture(unittest.TestCase):
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

class CoreProvenance(Fixture):
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

class CoreProvenanceCheck(Fixture):
    """The record as build-core.sh writes it, compared as make-package.sh does."""
    CHECKED = "source check: work/boxer equals pin + series (scripts/reproduce --source-only)"

    def write_record(self, check_line):
        rc, out = self.record()
        self.assertEqual(rc, 0)
        lines = out + (check_line + "\n" if check_line is not None else "") + "library: " + "c" * 64 + "\n"
        self.info = os.path.join(self.root, "BUILDINFO.txt")
        open(self.info, "w").write(lines)

    def check(self):
        r = subprocess.run(["bash", CHECK, self.root, self.info], capture_output=True, text=True)
        return r.returncode, r.stdout

    def test_checked_and_unchanged_passes(self):
        self.write_record(self.CHECKED)
        self.assertEqual(self.check(), (0, ""))

    def test_changed_series_is_reported(self):
        self.write_record(self.CHECKED)
        self.series("0002-b.patch", "0001-a.patch")
        rc, out = self.check()
        self.assertEqual(rc, 1)
        self.assertIn("0001-a.patch", out)

    def test_unchecked_source_is_refused(self):
        self.write_record("source check: UNCHECKED source root /elsewhere (BOXER_CORE_UNCHECKED_SOURCE=1); "
                          "the pin and series above do not describe it")
        rc, out = self.check()
        self.assertEqual(rc, 3)
        self.assertIn("/elsewhere", out)

    def test_record_without_source_check_is_refused(self):
        # What build-core.sh wrote before, for a checked and an unchecked
        # tree alike: the two cannot be told apart.
        self.write_record(None)
        self.assertEqual(self.check()[0], 2)

    def test_missing_record_is_refused(self):
        self.info = os.path.join(self.root, "none.txt")
        self.assertEqual(self.check()[0], 2)

if __name__ == "__main__":
    unittest.main()
