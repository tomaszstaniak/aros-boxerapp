#!/usr/bin/env python3
"""Tests for tools/depmanifest.py, which build-ui.sh uses to write the
source manifest of BUILDINFO.txt, together with scripts/check-sources.sh,
which make-package.sh uses to compare it with the tree:
python3 tests/build-tooling/test_depmanifest.py"""
import os, shutil, subprocess, sys, tempfile, unittest


def write_text(path, data):
    with open(path, "w") as f:
        f.write(data)


def append_text(path, data):
    with open(path, "a") as f:
        f.write(data)


def read_text(path):
    with open(path) as f:
        return f.read()

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "depmanifest.py")
CHECK = os.path.join(HERE, "..", "..", "scripts", "check-sources.sh")

def esc(p):
    return p.replace("$", "$$").replace(" ", "\\ ")

class DepManifest(unittest.TestCase):
    def setUp(self):
        self.top = tempfile.mkdtemp(prefix="dep manifest ")
        self.root = os.path.join(self.top, "project root")
        self.sdk = os.path.join(self.top, "sdk")
        for rel in ("src/ui/main.cpp", "src/model/gamebox.h", "src/model/fs util.h",
                    "work/boxer/DOSBox/include/dosbox.h", "build/abiv11/gen/art.inc"):
            self.put(os.path.join(self.root, rel), rel + "\n")
        self.put(os.path.join(self.sdk, "include/exec/types.h"), "sdk\n")
        self.obj = os.path.join(self.root, "build/abiv11/obj/main.o")
        self.put(self.obj, "o")
        deps = [os.path.join(self.root, r) for r in
                ("src/ui/main.cpp", "src/model/gamebox.h", "src/model/fs util.h",
                 "src/model/../model/gamebox.h", "work/boxer/DOSBox/include/dosbox.h",
                 "build/abiv11/gen/art.inc")] + [os.path.join(self.sdk, "include/exec/types.h")]
        write_text(self.obj + ".d", esc(self.obj) + ": " + " \\\n ".join(esc(d) for d in deps) + "\n")

    def tearDown(self):
        shutil.rmtree(self.top)

    def put(self, p, data):
        os.makedirs(os.path.dirname(p), exist_ok=True)
        write_text(p, data)

    def manifest(self, *objs):
        r = subprocess.run([sys.executable, TOOL, self.root, "--exclude", os.path.join(self.root, "build")]
                           + list(objs or [self.obj]), capture_output=True, text=True)
        return r.returncode, r.stdout

    def test_every_project_header_is_listed(self):
        rc, out = self.manifest()
        self.assertEqual(rc, 0)
        paths = [l.split("  ", 1)[1] for l in out.splitlines()]
        self.assertEqual(paths, ["src/model/fs util.h", "src/model/gamebox.h", "src/ui/main.cpp",
                                 "work/boxer/DOSBox/include/dosbox.h"])

    def test_changed_header_is_refused_by_the_package_check(self):
        rc, out = self.manifest()
        m = os.path.join(self.top, "sources.sha256")
        write_text(m, out)
        self.assertEqual(subprocess.run(["bash", CHECK, self.root, m]).returncode, 0)
        append_text(os.path.join(self.root, "src/model/gamebox.h"), "// changed\n")
        r = subprocess.run(["bash", CHECK, self.root, m], capture_output=True, text=True)
        self.assertEqual(r.returncode, 1)
        self.assertIn("src/model/gamebox.h", r.stdout)

    def test_missing_dependency_file_fails(self):
        os.remove(self.obj + ".d")
        self.assertEqual(self.manifest(), (2, ""))

    def test_missing_prerequisite_fails(self):
        os.remove(os.path.join(self.root, "src/model/gamebox.h"))
        self.assertEqual(self.manifest(), (2, ""))

    def test_nothing_in_the_project_fails(self):
        write_text(self.obj + ".d", esc(self.obj) + ": " + esc(os.path.join(self.sdk, "include/exec/types.h")) + "\n")
        self.assertEqual(self.manifest(), (2, ""))

if __name__ == "__main__":
    unittest.main()
