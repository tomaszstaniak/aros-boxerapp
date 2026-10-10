#!/usr/bin/env python3
"""Tests for scripts/check-sources.sh, the source check make-package.sh runs
before packaging: python3 tests/build-tooling/test_check_sources.py
CHECK_SOURCES=<script> runs the same cases against another implementation."""
import hashlib, os, shutil, subprocess, tempfile, unittest


def write_text(path, data):
    with open(path, "w") as f:
        f.write(data)


def append_text(path, data):
    with open(path, "a") as f:
        f.write(data)


def read_text(path):
    with open(path) as f:
        return f.read()

TOOL = os.environ.get("CHECK_SOURCES") or os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts", "check-sources.sh")

def sha(data):
    return hashlib.sha256(data.encode()).hexdigest()

class CheckSources(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="check sources ")
        self.files = {"src/model/gamebox.h": "header\n", "src/ui/main.cpp": "int main;\n",
                      "dir with space/a b.h": "spaced\n"}
        for rel, data in self.files.items():
            p = os.path.join(self.root, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            write_text(p, data)
        self.manifest = os.path.join(self.root, "manifest.sha256")

    def tearDown(self):
        shutil.rmtree(self.root)

    def write_manifest(self, text):
        write_text(self.manifest, text)

    def good(self):
        return "".join("%s  %s\n" % (sha(d), r) for r, d in sorted(self.files.items()))

    def run_check(self):
        r = subprocess.run(["bash", TOOL, self.root, self.manifest], capture_output=True, text=True)
        return r.returncode, r.stdout

    def test_unchanged_passes(self):
        self.write_manifest(self.good())
        self.assertEqual(self.run_check(), (0, ""))

    def test_changed_header_is_reported(self):
        self.write_manifest(self.good())
        append_text(os.path.join(self.root, "src/model/gamebox.h"), "// changed\n")
        rc, out = self.run_check()
        self.assertEqual(rc, 1)
        self.assertIn("src/model/gamebox.h", out)

    def test_removed_file_is_reported(self):
        self.write_manifest(self.good())
        os.remove(os.path.join(self.root, "dir with space/a b.h"))
        rc, out = self.run_check()
        self.assertEqual(rc, 1)
        self.assertIn("a b.h", out)

    def test_empty_manifest_is_refused(self):
        self.write_manifest("")
        self.assertEqual(self.run_check()[0], 2)

    def test_malformed_manifest_is_refused(self):
        self.write_manifest("not a checksum line\n")
        self.assertEqual(self.run_check()[0], 2)

    def test_partly_malformed_manifest_is_refused(self):
        self.write_manifest(self.good() + "abc  src/ui/main.cpp\n")
        self.assertEqual(self.run_check()[0], 2)

    def test_missing_manifest_is_refused(self):
        self.assertEqual(self.run_check()[0], 2)

if __name__ == "__main__":
    unittest.main()
