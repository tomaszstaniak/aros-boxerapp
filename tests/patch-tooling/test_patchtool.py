#!/usr/bin/env python3
"""Acceptance tests for scripts/patchtool.py: the profile's minimum tool
verification scenarios, on small disposable fixture repositories with
spaces in paths, binary files, symlinks and executable bits.

Run: tests/patch-tooling/run.sh   (or python3 -m unittest -v from this dir)
"""
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(os.path.dirname(HERE))
SCRIPTS = os.path.join(PROJECT, "scripts")
TOOLS = ["patchtool.py", "bootstrap.sh", "save-patch", "reproduce", "status"]

ENV = dict(os.environ)
ENV.update({"GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_AUTHOR_NAME": "Fixture", "GIT_AUTHOR_EMAIL": "fixture@example.invalid",
            "GIT_COMMITTER_NAME": "Fixture", "GIT_COMMITTER_EMAIL": "fixture@example.invalid"})
ENV.pop("PATCHTOOL_TEST_FAIL", None)

HEADER = ["--author", "Fixture", "--problem", "Fixture problem.", "--solution",
          "Fixture solution.", "--platform-scope", "all", "--upstream-status", "local-only",
          "--verification", "Not run (fixture).", "--removal-condition", "Never (fixture)."]


def sh(cwd, *cmd, env=None, check=True):
    p = subprocess.run(list(cmd), cwd=cwd, env=env or ENV, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
    if check and p.returncode != 0:
        raise AssertionError("%s failed (%d):\n%s" % (cmd, p.returncode, p.stdout))
    return p


def git(cwd, *args, check=True):
    return sh(cwd, "git", *args, check=check).stdout.strip()


def snapshot(root):
    """Byte-level snapshot of a directory, including .git and modes."""
    res = {}
    for dp, dn, fn in os.walk(root):
        for n in dn + fn:
            full = os.path.join(dp, n)
            rel = os.path.relpath(full, root)
            st = os.lstat(full)
            if os.path.islink(full):
                res[rel] = ("l", os.readlink(full))
            elif os.path.isdir(full):
                res[rel] = ("d", st.st_mode)
            else:
                res[rel] = ("f", st.st_mode, hashlib.sha256(open(full, "rb").read()).hexdigest(),
                            st.st_mtime_ns)
    return res


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="patchtool test ")
        self.src = os.path.join(self.tmp, "origin repo")
        os.makedirs(self.src)
        git(self.src, "init", "-q", "-b", "master")
        self.write("Other Sources/hello world.c", b'int main(void) { return 0; }\n')
        self.write("Other Sources/second file.c", b"int two;\n")
        self.write("Sample Games/logo.png", bytes(range(256)) * 8)
        self.write("Foo.framework/Versions/A/Foo", b"\x00\x01binary\xff" * 50)
        os.symlink("A", os.path.join(self.src, "Foo.framework/Versions/Current"))
        os.symlink("Versions/Current/Foo", os.path.join(self.src, "Foo.framework/Foo"))
        self.write("run.sh", b"#!/bin/sh\necho hi\n", mode=0o755)
        self.write(".gitignore", b"*.log\n")
        git(self.src, "add", "-A")
        git(self.src, "commit", "-q", "-m", "fixture base")
        self.pin = git(self.src, "rev-parse", "HEAD")
        self.proj = self.make_project("project dir")
        self.work = os.path.join(self.proj, "work", "fx")
        self.up = os.path.join(self.proj, "upstream", "fx")
        self.pdir = os.path.join(self.proj, "patches", "fx")

    def tearDown(self):
        subprocess.run(["chmod", "-R", "u+w", self.tmp])
        shutil.rmtree(self.tmp)

    def write(self, rel, data, root=None, mode=None):
        p = os.path.join(root or self.src, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data)
        if mode:
            os.chmod(p, mode)

    def make_project(self, name, series=True, pin=None):
        proj = os.path.join(self.tmp, name)
        os.makedirs(os.path.join(proj, "scripts"))
        for t in TOOLS:
            shutil.copy2(os.path.join(SCRIPTS, t), os.path.join(proj, "scripts", t))
        json.dump({"repositories": {"fx": {"url": self.src, "commit": pin or self.pin}}},
                  open(os.path.join(proj, "upstreams.json"), "w"))
        os.makedirs(os.path.join(proj, "patches", "fx"))
        if series:
            open(os.path.join(proj, "patches", "fx", "series"), "w").close()
        os.makedirs(os.path.join(proj, "work"))
        return proj

    def tool(self, name, *args, proj=None, env=None, rc=None):
        p = sh(self.tmp, os.path.join(proj or self.proj, "scripts", name), *args, env=env,
               check=False)
        if rc is not None:
            self.assertEqual(p.returncode, rc, "%s %s -> %d\n%s" % (name, args, p.returncode, p.stdout))
        return p

    def edit(self, rel, data, mode=None):
        self.write(rel, data, root=self.work, mode=mode)

    def save(self, title, *paths, rc=0, env=None, extra=()):
        return self.tool("save-patch", "--title", title, *HEADER, *extra, *paths, env=env, rc=rc)

    def series(self):
        return open(os.path.join(self.pdir, "series")).read().split()

    def baseline(self):
        return json.load(open(os.path.join(self.proj, "work", ".fx.baseline.json")))

    def assert_refused_recreate(self, keep):
        before = snapshot(self.work)
        p = self.tool("bootstrap.sh", "--recreate", rc=1)
        self.assertIn("REFUSED", p.stdout)
        self.assertEqual(snapshot(self.work), before, "refused recreate touched work")
        for rel, data in keep.items():
            self.assertEqual(open(os.path.join(self.work, rel), "rb").read(), data)
        return p

    def recreate_with_backup(self):
        bdir = os.path.join(self.tmp, "backup dir")
        p = self.tool("bootstrap.sh", "--recreate", "--backup-dir", bdir, rc=0)
        self.assertIn("recovery copy verified", p.stdout)
        copies = os.listdir(bdir)
        self.assertEqual(len(copies), 1)
        return os.path.join(bdir, copies[0])


class T1InitialPin(Fixture):
    def test_missing_configuration_is_distinct(self):
        bare = self.make_project("no series", series=False)
        p = self.tool("bootstrap.sh", proj=bare, rc=2)
        self.assertIn("missing series file", p.stdout)
        self.assertFalse(os.path.exists(os.path.join(bare, "work", "fx")))
        os.remove(os.path.join(bare, "upstreams.json"))
        self.assertIn("missing manifest", self.tool("bootstrap.sh", proj=bare, rc=2).stdout)
        short = self.make_project("short pin", pin=self.pin[:12])
        self.assertIn("40-hex", self.tool("bootstrap.sh", proj=short, rc=2).stdout)
        p = self.tool("bootstrap.sh", "--repo", "nope", rc=2)
        self.assertIn("not in the manifest", p.stdout)
        open(os.path.join(self.pdir, "series"), "w").write("0001-missing.patch\n")
        self.assertIn("file is missing", self.tool("bootstrap.sh", rc=2).stdout)

    def test_create_and_reproduce_unchanged_pin(self):
        p = self.tool("bootstrap.sh", rc=0)
        self.assertIn("created work/fx", p.stdout)
        self.assertEqual(git(self.work, "rev-parse", "HEAD"), self.pin)
        self.assertEqual(git(self.up, "rev-parse", "HEAD"), self.pin)
        self.assertEqual(snapshot(os.path.join(self.work, "Sample Games"))["logo.png"][2],
                         hashlib.sha256(bytes(range(256)) * 8).hexdigest())
        self.assertEqual(os.readlink(os.path.join(self.work, "Foo.framework/Foo")),
                         "Versions/Current/Foo")
        self.assertIn("synchronized", self.tool("bootstrap.sh", "--verify", rc=0).stdout)
        p = self.tool("reproduce", rc=0)
        self.assertIn("EQUIVALENT", p.stdout)
        self.assertIn("0 file(s)", p.stdout)
        self.tool("status", rc=0)
        b = self.baseline()
        self.assertEqual((b["pin"], b["series"], b["baseline_commit"]), (self.pin, [], self.pin))

    def test_dirty_upstream_blocks(self):
        self.tool("bootstrap.sh", rc=0)
        os.chmod(self.up, 0o755)
        open(os.path.join(self.up, "stray.txt"), "w").write("x")
        self.assertIn("not clean", self.tool("bootstrap.sh", "--verify", rc=1).stdout)
        self.assertIn("not clean", self.tool("reproduce", rc=1).stdout)


class T2Stale(Fixture):
    def setUp(self):
        super().setUp()
        self.tool("bootstrap.sh", rc=0)
        self.edit("Other Sources/hello world.c", b"int main(void) { return 1; }\n")
        self.save("first change", "Other Sources/hello world.c")
        self.edit("run.sh", b"#!/bin/sh\necho two\n")
        self.save("second change", "run.sh")
        self.tool("bootstrap.sh", "--verify", rc=0)

    def assert_stale(self, needle):
        p = self.tool("bootstrap.sh", "--verify", rc=1)
        self.assertIn("STALE", p.stdout)
        self.assertIn(needle, p.stdout)
        self.assertNotIn("is synchronized", p.stdout)
        self.assertIn("STALE", self.tool("reproduce", rc=1).stdout)
        self.assertEqual(self.tool("status").returncode, 1)
        # plain bootstrap on an existing, non-current copy changes nothing
        before = snapshot(self.work)
        self.tool("bootstrap.sh", rc=1)
        self.assertEqual(snapshot(self.work), before)
        p = self.save("blocked", "run.sh", rc=1)
        self.assertIn("stale", p.stdout)

    def test_new_pin(self):
        self.write("Other Sources/new upstream.c", b"int up;\n")
        git(self.src, "add", "-A")
        git(self.src, "commit", "-q", "-m", "upstream moves")
        new = git(self.src, "rev-parse", "HEAD")
        m = json.load(open(os.path.join(self.proj, "upstreams.json")))
        m["repositories"]["fx"]["commit"] = new
        json.dump(m, open(os.path.join(self.proj, "upstreams.json"), "w"))
        # upstream/ is at the old pin: reported, not silently moved
        self.assertIn("manifest pin", self.tool("bootstrap.sh", "--verify", rc=1).stdout)
        subprocess.run(["chmod", "-R", "u+w", self.up])
        shutil.rmtree(self.up)
        self.tool("bootstrap.sh", "--verify", rc=2)  # upstream missing: config error
        self.tool("bootstrap.sh", rc=1)  # re-clones upstream, refuses to touch work
        self.assert_stale("pin changed")
        old_head = git(self.work, "rev-parse", "HEAD")
        self.assert_refused_recreate({})  # saved tree differs from new pin + series
        copy = self.recreate_with_backup()
        self.assertEqual(git(copy, "rev-parse", "HEAD"), old_head)
        self.tool("bootstrap.sh", "--verify", rc=0)
        self.assertTrue(os.path.exists(os.path.join(self.work, "Other Sources/new upstream.c")))

    def test_appended_series(self):
        extra = os.path.join(self.pdir, "0007-extra.patch")
        open(extra, "w").write(
            "Title: hand-made\n\ndiff --git a/extra.txt b/extra.txt\nnew file mode 100644\n"
            "--- /dev/null\n+++ b/extra.txt\n@@ -0,0 +1 @@\n+extra\n")
        open(os.path.join(self.pdir, "series"), "a").write("0007-extra.patch\n")
        self.assert_stale("series order/content changed")
        self.assert_refused_recreate({})
        self.recreate_with_backup()
        self.tool("bootstrap.sh", "--verify", rc=0)

    def test_reordered_series_equivalent_tree(self):
        s = self.series()
        open(os.path.join(self.pdir, "series"), "w").write("\n".join(reversed(s)) + "\n")
        self.assert_stale("series order/content changed")
        # independent patches: same tree, clean, no extra history -> allowed
        p = self.tool("bootstrap.sh", "--recreate", rc=0)
        self.assertIn("no copy needed", p.stdout)
        self.assertEqual([x["name"] for x in self.baseline()["series"]], list(reversed(s)))
        self.tool("bootstrap.sh", "--verify", rc=0)

    def test_patch_edited_in_place(self):
        name = self.series()[0]
        path = os.path.join(self.pdir, name)
        txt = open(path).read().replace("+int main(void) { return 1; }",
                                        "+int main(void) { return 2; }")
        open(path, "w").write(txt)
        self.assert_stale("edited in place")
        self.assertIn("MISMATCH", self.tool("reproduce", rc=1).stdout)
        self.assert_refused_recreate({})
        self.recreate_with_backup()
        self.assertIn(b"return 2", open(os.path.join(self.work, "Other Sources/hello world.c"),
                                        "rb").read())


class T3ProtectUnsaved(Fixture):
    def setUp(self):
        super().setUp()
        self.tool("bootstrap.sh", rc=0)

    def check(self, keep):
        p = self.assert_refused_recreate(keep)
        copy = self.recreate_with_backup()
        for rel, data in keep.items():
            self.assertEqual(open(os.path.join(copy, rel), "rb").read(), data)
        self.tool("bootstrap.sh", "--verify", rc=0)
        return p

    def test_staged(self):
        self.edit("run.sh", b"staged\n")
        git(self.work, "add", "run.sh")
        self.assertIn("staged: 1", self.check({"run.sh": b"staged\n"}).stdout)

    def test_unstaged(self):
        self.edit("Other Sources/second file.c", b"unstaged\n")
        self.check({"Other Sources/second file.c": b"unstaged\n"})

    def test_untracked_addition(self):
        self.edit("Other Sources/brand new.c", b"new\n")
        self.assertIn("untracked: 1", self.check({"Other Sources/brand new.c": b"new\n"}).stdout)

    def test_deletion(self):
        os.remove(os.path.join(self.work, "Sample Games/logo.png"))
        p = self.assert_refused_recreate({})
        self.assertIn("deleted locally: Sample Games/logo.png", p.stdout)
        self.recreate_with_backup()

    def test_ignored_valuable(self):
        self.edit("notes from run.log", b"valuable\n")
        p = self.check({"notes from run.log": b"valuable\n"})
        self.assertIn("ignored (possibly valuable): 1", p.stdout)

    def test_failed_save_rolls_back(self):
        for stage in ("after-patch-file", "after-series", "after-commit"):
            self.edit("Other Sources/hello world.c", b"edit " + stage.encode() + b"\n")
            base = open(os.path.join(self.proj, "work", ".fx.baseline.json"), "rb").read()
            env = dict(ENV, PATCHTOOL_TEST_FAIL=stage)
            p = self.save("will fail", "Other Sources/hello world.c", env=env, rc=None)
            self.assertNotEqual(p.returncode, 0)
            self.assertEqual(self.series(), [])
            self.assertEqual([f for f in os.listdir(self.pdir) if f != "series"], [])
            self.assertEqual(open(os.path.join(self.proj, "work", ".fx.baseline.json"),
                                  "rb").read(), base)
            self.assertEqual(git(self.work, "rev-parse", "HEAD"), self.pin)
            self.assertEqual(open(os.path.join(self.work, "Other Sources/hello world.c"),
                                  "rb").read(), b"edit " + stage.encode() + b"\n")
        self.check({"Other Sources/hello world.c": b"edit after-commit\n"})

    def test_failed_patch_application(self):
        open(os.path.join(self.pdir, "0001-broken.patch"), "w").write(
            "Title: broken\n\ndiff --git a/run.sh b/run.sh\n--- a/run.sh\n+++ b/run.sh\n"
            "@@ -1,2 +1,2 @@\n #!/bin/sh\n-echo not-there\n+echo x\n")
        open(os.path.join(self.pdir, "series"), "w").write("0001-broken.patch\n")
        self.edit("run.sh", b"local edit\n")
        before = snapshot(self.work)
        p = self.tool("bootstrap.sh", "--recreate", "--backup-dir",
                      os.path.join(self.tmp, "bk"), rc=1)
        self.assertIn("does not apply", p.stdout)
        self.assertEqual(snapshot(self.work), before)
        self.assertEqual(self.baseline()["series"], [])
        # fresh project: no partial work copy or baseline is left behind
        fresh = self.make_project("fresh")
        shutil.copytree(self.pdir, os.path.join(fresh, "patches", "fx"), dirs_exist_ok=True)
        self.tool("bootstrap.sh", proj=fresh, rc=1)
        self.assertEqual(sorted(os.listdir(os.path.join(fresh, "work"))), [])


class T4LocalHistory(Fixture):
    def test_stashes_and_branch(self):
        self.tool("bootstrap.sh", rc=0)
        for i in (1, 2):
            self.edit("run.sh", b"stash %d\n" % i)
            git(self.work, "stash", "-q")
        git(self.work, "checkout", "-q", "-b", "side")
        self.edit("side file.txt", b"side\n")
        git(self.work, "add", "-A")
        git(self.work, "commit", "-q", "-m", "unique side commit")
        side = git(self.work, "rev-parse", "HEAD")
        git(self.work, "checkout", "-q", "aros-port")
        self.assertEqual(git(self.work, "status", "--porcelain"), "")
        self.assertIn("EQUIVALENT", self.tool("reproduce", rc=0).stdout)
        p = self.assert_refused_recreate({})
        self.assertIn("stash", p.stdout)
        self.assertIn("refs/heads/side", p.stdout)
        stashes = git(self.work, "log", "-g", "--format=%H", "refs/stash").split()
        self.assertEqual(len(stashes), 2)
        copy = self.recreate_with_backup()
        self.assertEqual(git(copy, "log", "-g", "--format=%H", "refs/stash").split(), stashes)
        self.assertEqual(git(copy, "rev-parse", "side"), side)
        self.assertEqual(git(copy, "show", "stash@{1}:run.sh"), "stash 1")
        self.assertEqual(git(copy, "show", "stash@{0}:run.sh"), "stash 2")
        self.tool("bootstrap.sh", "--verify", rc=0)

    def test_dropped_stash_is_found(self):
        self.tool("bootstrap.sh", rc=0)
        self.edit("run.sh", b"dropped\n")
        git(self.work, "stash", "-q")
        git(self.work, "stash", "drop", "-q")
        p = self.assert_refused_recreate({})
        self.assertIn("unreachable commit", p.stdout)


class T5Amend(Fixture):
    def setUp(self):
        super().setUp()
        self.tool("bootstrap.sh", rc=0)
        self.edit("Other Sources/hello world.c", b"saved version\n")
        self.save("saved change", "Other Sources/hello world.c")
        self.name = self.series()[0]
        self.pbytes = open(os.path.join(self.pdir, self.name), "rb").read()
        self.saved = git(self.work, "rev-parse", "HEAD")
        self.subject = git(self.work, "log", "-1", "--format=%s")

    def unchanged_identity(self):
        self.assertEqual(open(os.path.join(self.pdir, self.name), "rb").read(), self.pbytes)
        self.assertEqual(self.baseline()["series"][0]["sha256"],
                         hashlib.sha256(self.pbytes).hexdigest())
        self.assertEqual(git(self.work, "log", "-1", "--format=%s"), self.subject)
        self.assertEqual(git(self.work, "status", "--porcelain"), "")

    def test_content_amend(self):
        self.edit("Other Sources/hello world.c", b"amended version\n")
        git(self.work, "commit", "-q", "-a", "--amend", "--no-edit")
        amended = git(self.work, "rev-parse", "HEAD")
        self.unchanged_identity()
        p = self.tool("bootstrap.sh", "--verify", rc=1)
        self.assertIn("MISMATCH", p.stdout)
        self.assertIn("differs (content): Other Sources/hello world.c", p.stdout)
        self.assertIn("MISMATCH", self.tool("reproduce", rc=1).stdout)
        self.assertIn("amended", self.assert_refused_recreate({}).stdout)
        self.save("x", "run.sh", rc=1)
        copy = self.recreate_with_backup()
        self.assertEqual(git(copy, "rev-parse", "HEAD"), amended)
        self.assertEqual(git(copy, "show", "%s:Other Sources/hello world.c" % amended),
                         "amended version")
        self.assertEqual(git(copy, "cat-file", "-t", self.saved), "commit")
        self.assertEqual(open(os.path.join(self.work, "Other Sources/hello world.c"),
                              "rb").read(), b"saved version\n")

    def test_metadata_only_amend(self):
        git(self.work, "commit", "-q", "--amend", "--no-edit", "--date=2001-01-01T00:00:00")
        self.unchanged_identity()
        p = self.tool("bootstrap.sh", "--verify", rc=1)
        self.assertIn("source equivalence (saved tree vs reconstruction): EQUIVALENT", p.stdout)
        self.assertIn("not the saved baseline", p.stdout)
        self.assert_refused_recreate({})
        amended = git(self.work, "rev-parse", "HEAD")
        copy = self.recreate_with_backup()
        self.assertEqual(git(copy, "rev-parse", "HEAD"), amended)
        self.assertEqual(git(copy, "cat-file", "-t", self.saved), "commit")


class T6Successive(Fixture):
    def test_three_patches_deltas_and_numbering(self):
        self.tool("bootstrap.sh", rc=0)
        self.edit("Other Sources/hello world.c", b"one\n")
        self.save("first", "Other Sources/hello world.c")
        self.edit("Other Sources/second file.c", b"two\n")
        self.edit("run.sh", b"not selected\n")
        self.save("second", "Other Sources/second file.c")
        # gap and a stray existing file: neither may be overwritten
        stray = os.path.join(self.pdir, "0005-someone-elses.patch")
        open(stray, "w").write("do not touch\n")
        self.edit("Other Sources/hello world.c", b"three\n")
        self.save("third", "Other Sources/hello world.c")
        s = self.series()
        self.assertEqual(s, ["0001-first.patch", "0002-second.patch", "0006-third.patch"])
        self.assertEqual(open(stray).read(), "do not touch\n")
        p1, p2, p3 = (open(os.path.join(self.pdir, n)).read() for n in s)
        self.assertNotIn("hello world.c", p2)
        self.assertNotIn("run.sh", p2)
        self.assertIn("-one\n+three", p3)
        self.assertIn("Preceding-Patches:\n  0001-first.patch\n  0002-second.patch", p3)
        for field in ("Title:", "Repository: fx", "Date:", "Author:", "Base-Commit: " + self.pin,
                      "Problem:", "Solution:", "Platform-Scope:", "Upstream-Status: local-only",
                      "Verification:", "Removal-Condition:"):
            self.assertIn(field, p1)
        self.assertEqual(open(os.path.join(self.work, "run.sh"), "rb").read(), b"not selected\n")
        p = self.tool("reproduce", rc=0)
        self.assertIn("EQUIVALENT", p.stdout)
        self.assertIn("modified locally (content): run.sh", p.stdout)
        # empty selection and bad header are refused before any mutation
        before = (self.series(), self.baseline())
        self.save("nothing", "Other Sources/hello world.c", rc=1)
        self.tool("save-patch", "--title", "no why", "run.sh", rc=1)
        self.save("submitted", "run.sh", extra=["--upstream-status", "submitted"], rc=1)
        self.assertEqual((self.series(), self.baseline()), before)
        # --staged and header file
        hdr = os.path.join(self.tmp, "header file.txt")
        open(hdr, "w").write("Title: from file\nAuthor: F\nProblem: multi\n  line problem\n"
                             "Solution: s\nPlatform-Scope: AROS\nUpstream-Status: candidate\n"
                             "Verification: not run\nRemoval-Condition: upstream fix\n")
        git(self.work, "add", "run.sh")
        self.tool("save-patch", "--header", hdr, "--staged", rc=0)
        self.assertEqual(self.series()[-1], "0007-from-file.patch")
        self.assertIn("Problem:\n  multi\n  line problem", open(
            os.path.join(self.pdir, "0007-from-file.patch")).read())
        self.tool("bootstrap.sh", "--verify", rc=0)


class T7Binary(Fixture):
    def test_binary_roundtrip_and_unsupported(self):
        self.tool("bootstrap.sh", rc=0)
        self.edit("Sample Games/logo.png", bytes(reversed(range(256))) * 9 + b"\x00")
        self.edit("Sample Games/new image.bin", os.urandom(4096))
        os.remove(os.path.join(self.work, "Foo.framework/Versions/A/Foo"))
        os.remove(os.path.join(self.work, "Foo.framework/Versions/Current"))
        os.makedirs(os.path.join(self.work, "Foo.framework/Versions/B"))
        self.edit("Foo.framework/Versions/B/Foo", b"\x7fELF\x00new")
        os.symlink("B", os.path.join(self.work, "Foo.framework/Versions/Current"))
        os.chmod(os.path.join(self.work, "Other Sources/second file.c"), 0o755)
        self.save("binary and links", "Sample Games", "Foo.framework", "Other Sources")
        patch = open(os.path.join(self.pdir, self.series()[0]), "rb").read()
        self.assertIn(b"GIT binary patch", patch)
        self.assertNotIn(b"Binary files ", patch)
        out = os.path.join(self.tmp, "repro out")
        os.makedirs(out)
        self.assertIn("EQUIVALENT", self.tool("reproduce", "--out", out, "--strict", rc=0).stdout)
        r = os.path.join(out, "repro fx")
        for rel in ("Sample Games/logo.png", "Sample Games/new image.bin",
                    "Foo.framework/Versions/B/Foo"):
            self.assertEqual(open(os.path.join(r, rel), "rb").read(),
                             open(os.path.join(self.work, rel), "rb").read(), rel)
        self.assertFalse(os.path.exists(os.path.join(r, "Foo.framework/Versions/A/Foo")))
        self.assertEqual(os.readlink(os.path.join(r, "Foo.framework/Versions/Current")), "B")
        self.assertTrue(os.access(os.path.join(r, "Other Sources/second file.c"), os.X_OK))
        # unsupported: a nested repository (gitlink) is rejected before mutation
        nested = os.path.join(self.work, "nested repo")
        os.makedirs(nested)
        git(nested, "init", "-q")
        open(os.path.join(nested, "f"), "w").write("n")
        git(nested, "add", "f")
        git(nested, "commit", "-q", "-m", "n")
        before = (self.series(), self.baseline(), snapshot(nested))
        p = self.save("gitlink", "nested repo", rc=1)
        self.assertIn("gitlink", p.stdout)
        self.assertEqual((self.series(), self.baseline(), snapshot(nested)), before)


class T9SourceOnly(Fixture):
    """reproduce --source-only: what build-core.sh runs before compiling."""
    def setUp(self):
        super().setUp()
        self.tool("bootstrap.sh", rc=0)
        self.edit("run.sh", b"#!/bin/sh\necho two\n")
        self.save("a change", "run.sh")

    def test_equivalent(self):
        p = self.tool("reproduce", "--source-only", rc=0)
        self.assertIn("work equals pin + series", p.stdout)

    def test_stale_record_with_same_source_passes(self):
        # Rewording a patch header changes its digest (baseline STALE) but not
        # the tree it produces: plain reproduce fails, the source check passes.
        name = self.series()[0]
        path = os.path.join(self.pdir, name)
        txt = open(path).read().replace("Fixture problem.", "Reworded problem.")
        open(path, "w").write(txt)
        self.assertIn("STALE", self.tool("reproduce", rc=1).stdout)
        p = self.tool("reproduce", "--source-only", rc=0)
        self.assertIn("STALE", p.stdout)
        self.assertIn("work equals pin + series", p.stdout)

    def test_patch_content_changed_fails(self):
        name = self.series()[0]
        path = os.path.join(self.pdir, name)
        txt = open(path).read().replace("+echo two", "+echo three")
        open(path, "w").write(txt)
        p = self.tool("reproduce", "--source-only", rc=1)
        self.assertIn("MISMATCH", p.stdout)
        self.assertIn("work DIFFERS", p.stdout)

    def test_unsaved_or_staged_change_fails(self):
        self.edit("Other Sources/second file.c", b"int three;\n")
        self.assertIn("work DIFFERS", self.tool("reproduce", "--source-only", rc=1).stdout)
        git(self.work, "add", "-A")
        self.assertIn("work DIFFERS", self.tool("reproduce", "--source-only", rc=1).stdout)

    def test_missing_work_fails(self):
        subprocess.run(["chmod", "-R", "u+w", self.work])
        shutil.rmtree(self.work)
        self.tool("reproduce", "--source-only", rc=1)


class T8ReproduceElsewhere(Fixture):
    def test_reproduce_leaves_active_copies_alone(self):
        self.tool("bootstrap.sh", rc=0)
        self.edit("Other Sources/hello world.c", b"saved\n")
        self.save("saved", "Other Sources/hello world.c")
        self.edit("run.sh", b"unsaved\n")
        git(self.work, "add", "run.sh")
        self.edit("local extra.txt", b"x\n")
        self.edit("debug.log", b"log\n")
        bw, bu = snapshot(self.work), snapshot(self.up)
        p = self.tool("reproduce", rc=0)
        self.assertEqual(snapshot(self.work), bw)
        self.assertEqual(snapshot(self.up), bu)
        self.assertIn("EQUIVALENT", p.stdout)
        for s in ("modified locally (content): run.sh", "extra local file: local extra.txt",
                  "extra local file: debug.log", "staged M run.sh"):
            self.assertIn(s, p.stdout)
        self.tool("reproduce", "--strict", rc=1)
        self.tool("status", rc=1)
        self.assertEqual(snapshot(self.work), bw)
        # a second project with only manifest, series, patches and scripts
        other = self.make_project("other checkout")
        shutil.copytree(self.pdir, os.path.join(other, "patches", "fx"), dirs_exist_ok=True)
        self.tool("bootstrap.sh", proj=other, rc=0)
        self.assertEqual(git(os.path.join(other, "work", "fx"), "rev-parse", "HEAD^{tree}"),
                         git(self.work, "rev-parse", "HEAD^{tree}"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
