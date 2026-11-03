#!/usr/bin/env python3
"""Tests for the Raspberry Pi C++ runtime: the output-directory guard of
scripts/build-cxx-runtime.sh, the pin checks of tools/cxxrt_pins.py and
scripts/check-cxx-runtime.sh (what build-core.sh, build-ui.sh and
make-package.sh run):
python3 tests/build-tooling/test_cxx_runtime.py
No toolchain is needed: the build script is run in a copy of the project
with a stub compiler, and every case stops before anything is built."""
import hashlib, os, shutil, subprocess, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, "..", ".."))
PINS_TOOL = os.path.join(TOP, "tools", "cxxrt_pins.py")
MARKER = ".build-cxx-runtime-owned"
MARKER_TEXT = "directory owned by scripts/build-cxx-runtime.sh; deleted and rebuilt by it\n"


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb" if isinstance(data, bytes) else "w") as f:
        f.write(data)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(cmd, env=None):
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    return r.returncode, r.stdout + r.stderr


class Fixture(unittest.TestCase):
    """A copy of the project's pieces in a temporary root, with fake inputs."""

    def setUp(self):
        self.tmp = os.path.realpath(tempfile.mkdtemp(prefix="cxxrt test "))
        self.root = os.path.join(self.tmp, "project")
        for f in ("scripts/env.sh", "scripts/build-cxx-runtime.sh", "scripts/check-cxx-runtime.sh",
                  "tools/cxxrt_pins.py"):
            os.makedirs(os.path.dirname(os.path.join(self.root, f)), exist_ok=True)
            shutil.copy(os.path.join(TOP, f), os.path.join(self.root, f))
            os.chmod(os.path.join(self.root, f), 0o755)
        self.third = os.path.join(self.root, "third_party", "llvm-runtimes-aros")
        self.tarball = os.path.join(self.tmp, "llvm-project-20.1.0.src.tar.xz")
        write(self.tarball, b"tarball")
        self.diffs = {}
        for c in ("libcxx", "libcxxabi", "libunwind"):
            name = c + "-20.1.0.src-aros.diff"
            self.diffs[name] = ("patch for " + c + "\n").encode()
            write(os.path.join(self.third, name), self.diffs[name])
        self.archives = {n: ("archive " + n).encode() for n in ("libc++.a", "libc++abi.a", "libunwind.a")}
        self.pins = os.path.join(self.third, "PINS")
        lines = ["# test pins", "tarball %s llvm-project-20.1.0.src.tar.xz" % sha(b"tarball")]
        lines += ["diff %s %s" % (sha(d), n) for n, d in sorted(self.diffs.items())]
        lines += ["archive %s %s" % (sha(d), n) for n, d in sorted(self.archives.items())]
        write(self.pins, "\n".join(lines) + "\n")
        self.pin_lines = [l for l in lines if not l.startswith("#")]
        self.tc = os.path.join(self.tmp, "toolchain")
        for t in ("aarch64-aros-g++", "aarch64-aros-gcc", "aarch64-aros-clang", "aarch64-aros-clang++"):
            write(os.path.join(self.tc, t), "#!/bin/sh\nexit 0\n")
            os.chmod(os.path.join(self.tc, t), 0o755)
        self.sdk = os.path.join(self.tmp, "sdk")
        os.makedirs(os.path.join(self.sdk, "include"))
        self.home = os.path.join(self.tmp, "home")
        os.makedirs(self.home)
        self.deps = os.path.join(self.root, "deps")
        os.makedirs(self.deps)

    def tearDown(self):
        for d, _, fs in os.walk(self.tmp):
            os.chmod(d, 0o755)
        shutil.rmtree(self.tmp)

    def env(self, cxxrt, tarball=None, home=None):
        e = {k: v for k, v in os.environ.items() if not k.startswith(("AROS_", "BOXER_"))}
        e.update(AROS_RASPI_TOOLCHAIN=self.tc, AROS_RASPI_SDK=self.sdk, AROS_RASPI_CXXRT=cxxrt,
                 AROS_RASPI_LLVM_TARBALL=tarball or self.tarball, HOME=home or self.home)
        return e

    def build(self, cxxrt, **kw):
        return run([os.path.join(self.root, "scripts", "build-cxx-runtime.sh")], self.env(cxxrt, **kw))

    def runtime(self, where, archives=None, record=None):
        """A built runtime directory as build-cxx-runtime.sh leaves it."""
        for n, d in (archives or self.archives).items():
            write(os.path.join(where, "lib", n), d)
        write(os.path.join(where, MARKER), MARKER_TEXT)
        write(os.path.join(where, "BUILDINFO.txt"),
              "built: now\npins: x\n" + "\n".join(record if record is not None else self.pin_lines) + "\n")


class OutputGuard(Fixture):
    """Every refusal happens before anything is deleted."""

    def refused(self, cxxrt, words, **kw):
        rc, out = self.build(cxxrt, **kw)
        self.assertEqual(rc, 2, out)
        self.assertIn("refusing", out)
        self.assertIn(words, out)
        return out

    def test_root(self):
        self.refused("/", "resolves to /")

    def test_home(self):
        self.refused(self.home, "resolves to $HOME", home=self.home)

    def test_home_inside_drawer_is_still_home(self):
        home = os.path.join(self.deps, "home")
        os.makedirs(home)
        self.refused(home, "resolves to $HOME", home=home)

    def test_outside_the_drawer(self):
        outside = os.path.join(self.tmp, "elsewhere")
        os.makedirs(outside)
        write(os.path.join(outside, "keep"), "keep")
        self.refused(outside, "is not inside")
        self.assertTrue(os.path.exists(os.path.join(outside, "keep")))

    def test_project_root_and_drawer_itself(self):
        self.refused(self.root, "is not inside")
        self.refused(self.deps, "is not inside")

    def test_dotdot_out_of_the_drawer(self):
        self.refused(os.path.join(self.deps, "..", "scripts"), "is not inside")
        self.assertTrue(os.path.exists(os.path.join(self.root, "scripts", "env.sh")))

    def test_symlink_out_of_the_drawer(self):
        outside = os.path.join(self.tmp, "target")
        os.makedirs(outside)
        write(os.path.join(outside, "keep"), "keep")
        os.symlink(outside, os.path.join(self.deps, "link"))
        self.refused(os.path.join(self.deps, "link"), "is not inside")
        self.assertTrue(os.path.exists(os.path.join(outside, "keep")))

    def test_drawer_symlinked_outside_the_project(self):
        os.rmdir(self.deps)
        outside = os.path.join(self.tmp, "deps-elsewhere")
        os.makedirs(outside)
        os.symlink(outside, self.deps)
        rc, out = self.build(os.path.join(self.deps, "cxxrt"))
        self.assertEqual(rc, 2, out)
        self.assertIn("resolves outside the project", out)

    def test_existing_directory_without_marker(self):
        d = os.path.join(self.deps, "cxxrt")
        write(os.path.join(d, "keep"), "keep")
        self.refused(d, "has no " + MARKER)
        self.assertTrue(os.path.exists(os.path.join(d, "keep")))

    def test_wrong_marker_content(self):
        d = os.path.join(self.deps, "cxxrt")
        write(os.path.join(d, MARKER), "something else\n")
        write(os.path.join(d, "keep"), "keep")
        self.refused(d, "has no " + MARKER)
        self.assertTrue(os.path.exists(os.path.join(d, "keep")))

    def test_work_directory_without_marker(self):
        d = os.path.join(self.deps, "cxxrt")
        write(os.path.join(d, MARKER), MARKER_TEXT)
        write(os.path.join(d + "-work", "keep"), "keep")
        out = self.refused(d, "work directory")
        self.assertIn("has no " + MARKER, out)
        self.assertTrue(os.path.exists(os.path.join(d + "-work", "keep")))

    def test_a_file_in_place_of_the_directory(self):
        f = os.path.join(self.deps, "cxxrt")
        write(f, "file")
        self.refused(f, "is not a directory")
        self.assertTrue(os.path.isfile(f))

    def test_owned_directory_passes_the_guard_and_input_check_comes_first(self):
        """With a marked directory the guard passes; a wrong tarball then
        stops the build, and the directory is not touched."""
        d = os.path.join(self.deps, "cxxrt")
        write(os.path.join(d, MARKER), MARKER_TEXT)
        write(os.path.join(d, "keep"), "keep")
        bad = os.path.join(self.tmp, "other.tar.xz")
        write(bad, b"not the tarball")
        rc, out = self.build(d, tarball=bad)
        self.assertEqual(rc, 2, out)
        self.assertNotIn("refusing", out)
        self.assertIn("MISMATCH tarball", out)
        self.assertTrue(os.path.exists(os.path.join(d, "keep")))

    def test_new_directory_passes_the_guard(self):
        d = os.path.join(self.deps, "aros-aarch64-abiv1-raspi", "cxxrt")
        bad = os.path.join(self.tmp, "other.tar.xz")
        write(bad, b"not the tarball")
        rc, out = self.build(d, tarball=bad)
        self.assertEqual(rc, 2, out)
        self.assertNotIn("refusing", out)
        self.assertIn("not the pinned LLVM tarball", out)
        self.assertFalse(os.path.exists(d))

    def test_changed_diff_stops_before_deleting(self):
        d = os.path.join(self.deps, "cxxrt")
        write(os.path.join(d, MARKER), MARKER_TEXT)
        write(os.path.join(d, "keep"), "keep")
        write(os.path.join(self.third, "libunwind-20.1.0.src-aros.diff"), b"changed\n")
        rc, out = self.build(d)
        self.assertEqual(rc, 2, out)
        self.assertIn("MISMATCH diff libunwind-20.1.0.src-aros.diff", out)
        self.assertIn("differ from", out)
        self.assertTrue(os.path.exists(os.path.join(d, "keep")))


class Pins(Fixture):
    def tool(self, *args):
        return run(["python3", PINS_TOOL] + list(args))

    def test_all_match(self):
        d = os.path.join(self.deps, "cxxrt")
        self.runtime(d)
        self.assertEqual(self.tool("tarball", self.pins, self.tarball)[0], 0)
        self.assertEqual(self.tool("diffs", self.pins, self.third)[0], 0)
        self.assertEqual(self.tool("archives", self.pins, os.path.join(d, "lib"))[0], 0)
        self.assertEqual(self.tool("record", self.pins, os.path.join(d, "BUILDINFO.txt"), "")[0], 0)

    def test_tarball_mismatch(self):
        write(self.tarball, b"tarball, but changed")
        rc, out = self.tool("tarball", self.pins, self.tarball)
        self.assertEqual(rc, 1)
        self.assertIn("MISMATCH tarball llvm-project-20.1.0.src.tar.xz", out)

    def test_each_diff_mismatch_and_missing(self):
        for n in self.diffs:
            p = os.path.join(self.third, n)
            write(p, b"other\n")
            rc, out = self.tool("diffs", self.pins, self.third)
            self.assertEqual(rc, 1, n)
            self.assertIn("MISMATCH diff " + n, out)
            os.remove(p)
            rc, out = self.tool("diffs", self.pins, self.third)
            self.assertEqual(rc, 1, n)
            self.assertIn("MISSING  diff " + n, out)
            write(p, self.diffs[n])

    def test_each_archive_mismatch(self):
        for n in self.archives:
            d = os.path.join(self.tmp, "rt-" + n)
            changed = dict(self.archives)
            changed[n] = b"rebuilt differently"
            self.runtime(d, archives=changed)
            rc, out = self.tool("archives", self.pins, os.path.join(d, "lib"))
            self.assertEqual(rc, 1, n)
            self.assertIn("MISMATCH archive " + n, out)

    def test_record_missing_and_extra(self):
        d = os.path.join(self.tmp, "rt")
        self.runtime(d, record=self.pin_lines[:-1])
        rc, out = self.tool("record", self.pins, os.path.join(d, "BUILDINFO.txt"), "")
        self.assertEqual(rc, 1)
        self.assertIn("NOT RECORDED", out)
        self.runtime(d, record=self.pin_lines + ["archive %s libextra.a" % ("0" * 64)])
        rc, out = self.tool("record", self.pins, os.path.join(d, "BUILDINFO.txt"), "")
        self.assertEqual(rc, 1)
        self.assertIn("NOT PINNED", out)

    def test_malformed_pins(self):
        write(self.pins, "tarball nothex llvm.tar.xz\n")
        self.assertEqual(self.tool("lines", self.pins)[0], 2)
        write(self.pins, "tarball %s x\n" % ("0" * 64))
        rc, out = self.tool("lines", self.pins)
        self.assertEqual(rc, 2)
        self.assertIn("no diff pin", out)


class CheckRuntime(Fixture):
    def check(self, *args, tarball=None):
        e = {k: v for k, v in os.environ.items() if not k.startswith("AROS_")}
        if tarball:
            e["AROS_RASPI_LLVM_TARBALL"] = tarball
        return run([os.path.join(self.root, "scripts", "check-cxx-runtime.sh")] + list(args), e)

    def ui_info(self, lines):
        p = os.path.join(self.tmp, "ui", "BUILDINFO.txt")
        write(p, "abi: aros-aarch64-abiv1-raspi\n" + "".join("c++ runtime: %s\n" % l for l in lines)
              + "sha256: x\n")
        return p

    def test_passes(self):
        d = os.path.join(self.deps, "cxxrt")
        self.runtime(d)
        rc, out = self.check(d, self.ui_info(self.pin_lines), tarball=self.tarball)
        self.assertEqual(rc, 0, out)

    def test_archive_changed_after_the_build(self):
        d = os.path.join(self.deps, "cxxrt")
        self.runtime(d)
        write(os.path.join(d, "lib", "libc++.a"), b"replaced")
        rc, out = self.check(d)
        self.assertEqual(rc, 1, out)
        self.assertIn("MISMATCH archive libc++.a", out)

    def test_binary_linked_with_another_runtime(self):
        d = os.path.join(self.deps, "cxxrt")
        self.runtime(d)
        other = [l if not l.endswith(" libunwind.a") else "archive %s libunwind.a" % ("1" * 64)
                 for l in self.pin_lines]
        rc, out = self.check(d, self.ui_info(other))
        self.assertEqual(rc, 1, out)
        self.assertIn("NOT RECORDED archive", out)
        self.assertIn("NOT PINNED   archive", out)

    def test_wrong_tarball_when_named(self):
        d = os.path.join(self.deps, "cxxrt")
        self.runtime(d)
        bad = os.path.join(self.tmp, "bad.tar.xz")
        write(bad, b"x")
        rc, out = self.check(d, tarball=bad)
        self.assertEqual(rc, 1, out)
        self.assertIn("MISMATCH tarball", out)

    def test_patch_changed_in_the_tree(self):
        d = os.path.join(self.deps, "cxxrt")
        self.runtime(d)
        write(os.path.join(self.third, "libcxx-20.1.0.src-aros.diff"), b"edited\n")
        rc, out = self.check(d)
        self.assertEqual(rc, 1, out)
        self.assertIn("MISMATCH diff libcxx-20.1.0.src-aros.diff", out)


class RealPins(unittest.TestCase):
    """The pins in the repository match the patches next to them."""

    def test_repository_patches(self):
        third = os.path.join(TOP, "third_party", "llvm-runtimes-aros")
        rc, out = run(["python3", PINS_TOOL, "diffs", os.path.join(third, "PINS"), third])
        self.assertEqual(rc, 0, out)


if __name__ == "__main__":
    unittest.main()
