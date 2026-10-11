#!/usr/bin/env python3
"""Tests for the build-input and runtime-file records of BUILDINFO.txt:
tools/png2inc.py --inputs, tools/make-fonts.py --inputs, tools/depmanifest.py
--only and tools/inputmanifest.py (what build-ui.sh records), and
scripts/check-build-inputs.sh and scripts/check-runtime-files.sh (what
make-package.sh compares before packaging):
python3 tests/build-tooling/test_build_inputs.py
AROS_ISO_FONTS=<an AROS ISO's Fonts/> also runs make-fonts.py on the real
descriptions."""
import hashlib, os, shutil, struct, subprocess, sys, tempfile, unittest, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, "..", ".."))
PNG2INC = os.path.join(TOP, "tools", "png2inc.py")
FONTS = os.path.join(TOP, "tools", "make-fonts.py")
DEPMAN = os.path.join(TOP, "tools", "depmanifest.py")
MERGE = os.path.join(TOP, "tools", "inputmanifest.py")
CHECK_INPUTS = os.path.join(TOP, "scripts", "check-build-inputs.sh")
CHECK_RUNTIME = os.path.join(TOP, "scripts", "check-runtime-files.sh")


def write_bytes(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def append_bytes(path, data):
    with open(path, "ab") as f:
        f.write(data)


def read_text(path):
    with open(path) as f:
        return f.read()


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def png(r, g, b, a):
    """A 1x1 8-bit RGBA PNG."""
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes([0, r, g, b, a]))) + chunk(b"IEND", b""))


def esc(p):
    return p.replace("$", "$$").replace(" ", "\\ ")


def run(*cmd):
    r = subprocess.run(list(cmd), capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


class Fixture(unittest.TestCase):
    """A project as build-ui.sh leaves it: compiled objects with .d files,
    embedded art, a font input outside the project, an SDL3 header under
    build/, a stage/ with Fonts/ and BUILDINFO.txt written from the tools'
    own lists."""

    def setUp(self):
        self.top = tempfile.mkdtemp(prefix="build inputs ")
        self.root = os.path.join(self.top, "project root")
        self.iso = os.path.join(self.top, "iso", "Fonts")
        r = self.root
        self.art = os.path.join(r, "assets/runtime/boxer/Play Template.png")
        self.cover = os.path.join(r, "assets/runtime/boxer/CDCase-128.png")
        write_bytes(self.art, png(10, 20, 30, 255))
        write_bytes(self.cover, png(1, 2, 3, 128))
        self.otag = os.path.join(self.iso, "Vera Sans.otag")
        write_bytes(self.otag, b"otag v1\n")
        self.src = os.path.join(r, "src/ui/main.cpp")
        write_bytes(self.src, b"int main;\n")
        self.sdl = os.path.join(r, "build/abiv11/sdl3/src/SDL3-3.4.12/include/SDL3/SDL_audio.h")
        write_bytes(self.sdl, b"sdl audio\n")
        self.out = os.path.join(r, "build/abiv11/ui-sdl3")
        gen = os.path.join(self.out, "gen")
        os.makedirs(gen)
        self.assertEqual(run(sys.executable, PNG2INC, "--inputs", os.path.join(gen, "boxer_assets.inputs"),
                             os.path.join(gen, "boxer_assets.inc"), "Play=" + self.art)[0], 0)
        self.assertEqual(run(sys.executable, PNG2INC, "--inputs", os.path.join(gen, "cover_assets.inputs"),
                             os.path.join(gen, "cover_assets.inc"), "CDCase=" + self.cover + "@2")[0], 0)
        # make-fonts.py accepts only the real ISO descriptions (sha1 pinned);
        # its list has this form (FontsTool.test_real_iso_fonts runs it).
        with open(os.path.join(gen, "fonts.inputs"), "w") as f:
            f.write("%s  %s\n%s  %s\n" % (sha(self.otag), self.otag, sha(FONTS), FONTS))
        self.obj = os.path.join(self.out, "obj/ui-main.cpp.o")
        write_bytes(self.obj, b"o")
        deps = [self.src, self.sdl, os.path.join(gen, "boxer_assets.inc"), os.path.join(gen, "cover_assets.inc")]
        write_bytes(self.obj + ".d", (esc(self.obj) + ": " + " \\\n ".join(esc(d) for d in deps) + "\n").encode())
        rc, sources, err = run(sys.executable, DEPMAN, r, "--exclude", os.path.join(r, "build"),
                               "--exclude", self.out, self.obj)
        self.assertEqual(rc, 0, err)
        rc, tree, err = run(sys.executable, DEPMAN, r, "--only", os.path.join(r, "build"), "--only", self.out, self.obj)
        self.assertEqual(rc, 0, err)
        write_bytes(os.path.join(gen, "build-tree.inputs"), tree.encode())
        rc, inputs, err = run(sys.executable, MERGE, r, *sorted(
            os.path.join(gen, n) for n in os.listdir(gen) if n.endswith(".inputs")))
        self.assertEqual(rc, 0, err)
        self.stage = os.path.join(self.out, "stage")
        write_bytes(os.path.join(self.stage, "BoxerUI"), b"binary")
        write_bytes(os.path.join(self.stage, "Fonts/BoxerSans.otag"), b"modified otag\n")
        write_bytes(os.path.join(self.stage, "Fonts/BoxerSans.font"), b"\x0f\x03\x00\x00")
        runtime = "".join("%s  Fonts/%s\n" % (sha(os.path.join(self.stage, "Fonts", n)), n)
                          for n in sorted(os.listdir(os.path.join(self.stage, "Fonts"))))
        self.pkg = os.path.join(self.top, "package", "Boxer")
        write_bytes(os.path.join(self.pkg, "BoxerUI"), b"binary")
        shutil.copytree(os.path.join(self.stage, "Fonts"), os.path.join(self.pkg, "Fonts"))
        self.info = os.path.join(self.out, "BUILDINFO.txt")
        self.sections = {"sources": sources, "build inputs": inputs, "runtime files": runtime}
        self.write_info()

    def tearDown(self):
        shutil.rmtree(self.top)

    def write_info(self, omit=()):
        text = "abi: abiv11\nsha256: " + "b" * 64 + "\n"
        for head in ("sources", "build inputs", "runtime files"):
            if head in omit:
                continue
            text += head + ": (list)\n" + "".join("  " + l + "\n" for l in self.sections[head].splitlines())
        text += "core library: " + "c" * 64 + "  lib.a\n"
        write_bytes(self.info, text.encode())

    def inputs_check(self):
        return run("bash", CHECK_INPUTS, self.root, self.info)

    def runtime_check(self, d):
        return run("bash", CHECK_RUNTIME, self.info, d)


class Records(Fixture):
    def test_inputs_list_what_the_tools_read(self):
        listed = [l.split("  ", 1)[1] for l in self.sections["build inputs"].splitlines()]
        for rel in ("assets/runtime/boxer/Play Template.png", "assets/runtime/boxer/CDCase-128.png",
                    "build/abiv11/sdl3/src/SDL3-3.4.12/include/SDL3/SDL_audio.h",
                    "build/abiv11/ui-sdl3/gen/boxer_assets.inc", "build/abiv11/ui-sdl3/gen/cover_assets.inc"):
            self.assertIn(rel, listed)
        # Outside the project: as given. The generators record themselves.
        self.assertIn(self.otag, listed)
        self.assertIn(PNG2INC, listed)
        self.assertIn(FONTS, listed)
        self.assertNotIn("src/ui/main.cpp", listed)
        self.assertEqual(len(listed), len(set(listed)))

    def test_merge_refuses_a_file_changed_since_it_was_read(self):
        gen = os.path.join(self.out, "gen")
        append_bytes(self.art, b"x")
        rc, out, err = run(sys.executable, MERGE, self.root, os.path.join(gen, "boxer_assets.inputs"))
        self.assertEqual((rc, out), (2, ""))
        self.assertIn("Play Template.png", err)

    def test_merge_refuses_an_empty_list(self):
        empty = os.path.join(self.top, "empty.inputs")
        write_bytes(empty, b"")
        self.assertEqual(run(sys.executable, MERGE, self.root, empty)[0], 2)


class Checks(Fixture):
    def test_unchanged_complete_set_passes(self):
        self.assertEqual(self.inputs_check(), (0, "", ""))
        self.assertEqual(self.runtime_check(self.stage), (0, "", ""))
        self.assertEqual(self.runtime_check(self.pkg), (0, "", ""))

    def test_changed_asset(self):
        write_bytes(self.art, png(10, 20, 31, 255))
        rc, out, _ = self.inputs_check()
        self.assertEqual(rc, 1)
        self.assertIn("assets/runtime/boxer/Play Template.png: FAILED", out)
        self.assertEqual(self.runtime_check(self.pkg)[0], 0)

    def test_changed_font_input(self):
        append_bytes(self.otag, b"changed\n")
        rc, out, _ = self.inputs_check()
        self.assertEqual(rc, 1)
        self.assertIn(self.otag + ": FAILED", out)

    def test_changed_font_in_package(self):
        append_bytes(os.path.join(self.pkg, "Fonts/BoxerSans.otag"), b"x")
        self.assertEqual(self.inputs_check()[0], 0)
        self.assertEqual(self.runtime_check(self.stage)[0], 0)
        rc, out, _ = self.runtime_check(self.pkg)
        self.assertEqual(rc, 1)
        self.assertIn("Fonts/BoxerSans.otag: FAILED", out)

    def test_changed_staged_font(self):
        append_bytes(os.path.join(self.stage, "Fonts/BoxerSans.font"), b"x")
        rc, out, _ = self.runtime_check(self.stage)
        self.assertEqual(rc, 1)
        self.assertIn("Fonts/BoxerSans.font: FAILED", out)

    def test_changed_sdl3_header(self):
        append_bytes(self.sdl, b"// changed\n")
        rc, out, _ = self.inputs_check()
        self.assertEqual(rc, 1)
        self.assertIn("build/abiv11/sdl3/src/SDL3-3.4.12/include/SDL3/SDL_audio.h: FAILED", out)

    def test_changed_generator(self):
        # The generator's own path is outside this fixture's root; record
        # a copy inside it so the test does not edit the real tool.
        tool = os.path.join(self.root, "tools", "png2inc.py")
        os.makedirs(os.path.dirname(tool))
        shutil.copyfile(PNG2INC, tool)
        self.sections["build inputs"] += "%s  tools/png2inc.py\n" % sha(tool)
        self.write_info()
        append_bytes(tool, b"# changed\n")
        rc, out, _ = self.inputs_check()
        self.assertEqual(rc, 1)
        self.assertIn("tools/png2inc.py: FAILED", out)

    def test_missing_asset(self):
        os.remove(self.cover)
        rc, out, _ = self.inputs_check()
        self.assertEqual(rc, 1)
        self.assertIn("assets/runtime/boxer/CDCase-128.png: FAILED", out)

    def test_missing_font_in_package(self):
        os.remove(os.path.join(self.pkg, "Fonts/BoxerSans.font"))
        rc, out, _ = self.runtime_check(self.pkg)
        self.assertEqual(rc, 1)
        self.assertIn("Fonts/BoxerSans.font: FAILED", out)

    def test_extra_font_in_package(self):
        write_bytes(os.path.join(self.pkg, "Fonts/Other.otag"), b"other")
        rc, out, _ = self.runtime_check(self.pkg)
        self.assertEqual(rc, 1)
        self.assertIn("Fonts/Other.otag: not in the staging record", out)

    def test_record_without_build_inputs_is_refused(self):
        self.write_info(omit=("build inputs",))
        rc, out, err = self.inputs_check()
        self.assertEqual((rc, out), (2, ""))
        self.assertIn("no build input list", err)

    def test_record_without_runtime_files_is_refused(self):
        self.write_info(omit=("runtime files",))
        rc, out, err = self.runtime_check(self.pkg)
        self.assertEqual((rc, out), (2, ""))
        self.assertIn("no runtime file list", err)


@unittest.skipUnless(os.environ.get("AROS_ISO_FONTS"), "AROS_ISO_FONTS not set")
class FontsTool(unittest.TestCase):
    def test_real_iso_fonts(self):
        out = tempfile.mkdtemp(prefix="fonts ")
        try:
            lst = os.path.join(out, "fonts.inputs")
            rc, _, err = run(sys.executable, FONTS, "--inputs", lst, os.environ["AROS_ISO_FONTS"],
                             os.path.join(out, "Fonts"))
            self.assertEqual(rc, 0, err)
            rows = [l.split("  ", 1) for l in read_text(lst).splitlines()]
            names = sorted(os.path.basename(p) for _, p in rows)
            self.assertEqual(names, ["Vera Sans Bold.otag", "Vera Sans.otag", "make-fonts.py"])
            for h, p in rows:
                self.assertEqual(h, sha(p))
        finally:
            shutil.rmtree(out)


if __name__ == "__main__":
    unittest.main()
