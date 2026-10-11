#!/usr/bin/env python3
"""Tests for scripts/check-package-notices.sh, the last check make-package.sh
runs and the check a release uses:
python3 tests/build-tooling/test_package_notices.py"""
import os, shutil, subprocess, tempfile, unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
TOOL = os.path.join(ROOT, "scripts", "check-package-notices.sh")
DIAG = "DIAGNOSTIC BUILD, NOT FOR DISTRIBUTION: core built from an unverified source tree\n"


def write_text(path, data):
    with open(path, "w") as f:
        f.write(data)


class PackageNotices(unittest.TestCase):
    def setUp(self):
        self.pkg = tempfile.mkdtemp(prefix="package notices ")
        os.makedirs(os.path.join(self.pkg, "LICENSES"))
        shutil.copy(os.path.join(ROOT, "COPYING"), self.pkg)
        for l in ("AROS-APL-1.1", "GCC-exception-3.1", "LGPL-2.1", "Bitstream-Vera",
                  "BSD-2-Clause-ADBToolkit", "MIT-stb", "SDL3-zlib"):
            shutil.copy(os.path.join(ROOT, "LICENSES", l + ".txt"), os.path.join(self.pkg, "LICENSES"))
        for n in ("NOTICE-boxer.txt", "NOTICE-replacements.txt"):
            shutil.copy(os.path.join(ROOT, "assets", n), self.pkg)
        shutil.copy(os.path.join(ROOT, "documentation", "licenses.md"), self.pkg)
        write_text(os.path.join(self.pkg, "boxer-x-source.tar.gz"), "archive")
        self.buildinfo = "host: sdl3\npackage: boxer x x86_64-aros-v11\n"
        self.readme = "Boxer for AROS x\n"
        self.write()

    def tearDown(self):
        shutil.rmtree(self.pkg)

    def write(self):
        write_text(os.path.join(self.pkg, "BUILDINFO.txt"), self.buildinfo)
        write_text(os.path.join(self.pkg, "ReadMe.txt"), self.readme)

    def check(self, *flags):
        r = subprocess.run(["bash", TOOL, *flags, "abiv11", self.pkg], capture_output=True, text=True)
        return r.returncode, r.stdout

    def test_release_package_passes(self):
        self.assertEqual(self.check()[0], 0)

    def test_diagnostic_package_is_refused_as_release(self):
        self.buildinfo = DIAG + "\n" + self.buildinfo
        self.readme = DIAG + self.readme
        self.write()
        rc, out = self.check()
        self.assertEqual(rc, 1)
        self.assertIn("NOTFORRELEASE", out)
        self.assertEqual(self.check("--diagnostic")[0], 0)

    def test_marker_in_readme_alone_is_refused(self):
        self.readme = DIAG + self.readme
        self.write()
        self.assertEqual(self.check()[0], 1)

    def test_dry_run_package_is_refused_as_release(self):
        self.buildinfo += "WARNING: NOT CORRESPONDING SOURCE - src/ changed after this binary was built\n"
        self.write()
        self.assertEqual(self.check()[0], 1)
        self.assertEqual(self.check("--diagnostic")[0], 0)


if __name__ == "__main__":
    unittest.main()
