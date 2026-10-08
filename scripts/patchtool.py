#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Patch tooling for pinned upstream repositories (upstream-and-patches profile).

Subcommands: bootstrap, save-patch, reproduce, status.  Normally invoked
through the wrappers scripts/bootstrap.sh, scripts/save-patch,
scripts/reproduce and scripts/status.  The project root is the parent of the
directory holding this file, so the tool works from any cwd and with spaces
in paths.  Python 3 stdlib and git only; no ABI or toolchain is needed.

Exit codes: 0 ok, 1 check failed or action refused, 2 configuration error.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile

TOOL_VERSION = 1
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BRANCH = "aros-port"
REMOTE = "pinned-upstream"
STATUSES = ("candidate", "local-only", "submitted", "accepted")
# Materialization and save commits use a fixed identity: work/ Git only tracks
# the local diff (it is not project history), and must not depend on user config.
GIT_ID = ["-c", "user.name=patchtool", "-c", "user.email=patchtool@localhost"]
# Neutralize user config that changes diff output or path handling.
GIT_NEUTRAL = ["-c", "core.quotepath=off", "-c", "diff.noprefix=false",
               "-c", "diff.mnemonicprefix=false", "-c", "core.autocrlf=false"]


class ConfigError(Exception):
    pass


class Refused(Exception):
    pass


# ---------------------------------------------------------------- git helpers

def git(repo, *args, env=None, check=True, binary=False, inp=None):
    e = dict(os.environ)
    e["GIT_OPTIONAL_LOCKS"] = "0"  # read-only queries must not touch the index
    e["GIT_LITERAL_PATHSPECS"] = "1"
    e.pop("GIT_DIR", None)
    e.pop("GIT_WORK_TREE", None)
    if env:
        e.update(env)
    cmd = ["git"] + GIT_NEUTRAL + (["-C", repo] if repo else []) + list(args)
    p = subprocess.run(cmd, env=e, input=inp, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if check and p.returncode != 0:
        raise RuntimeError("git %s failed (%d): %s" % (
            " ".join(args), p.returncode, p.stderr.decode(errors="replace").strip()))
    if binary:
        return p
    p.stdout = p.stdout.decode("utf-8", errors="surrogateescape")
    p.stderr = p.stderr.decode("utf-8", errors="surrogateescape")
    return p


def gout(repo, *args, **kw):
    return git(repo, *args, **kw).stdout.strip()


def has_object(repo, sha):
    return git(repo, "cat-file", "-e", sha + "^{commit}", check=False).returncode == 0


def is_ancestor(repo, a, b):
    return git(repo, "merge-base", "--is-ancestor", a, b, check=False).returncode == 0


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def write_atomic(path, data):
    tmp = path + ".tmp-%d" % os.getpid()
    with open(tmp, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def fail_point(stage):
    """Test hook: simulate a crash at a named stage of a mutation."""
    if os.environ.get("PATCHTOOL_TEST_FAIL") == stage:
        raise RuntimeError("simulated failure at %s (PATCHTOOL_TEST_FAIL)" % stage)


# ------------------------------------------------------------- configuration

class Config:
    def __init__(self, repo_id=None):
        self.manifest = os.path.join(ROOT, "upstreams.json")
        if not os.path.isfile(self.manifest):
            raise ConfigError("missing manifest %s" % self.manifest)
        try:
            data = json.load(open(self.manifest))
        except ValueError as ex:
            raise ConfigError("manifest is not valid JSON: %s" % ex)
        if data.get("example_only"):
            raise ConfigError("upstreams.json still carries example_only; it is not operational")
        repos = data.get("repositories")
        if not isinstance(repos, dict) or not repos:
            raise ConfigError("manifest has no 'repositories' map")
        if repo_id is None:
            if len(repos) != 1:
                raise ConfigError("several repositories in manifest; pass --repo (%s)"
                                  % ", ".join(sorted(repos)))
            repo_id = next(iter(repos))
        if repo_id not in repos:
            raise ConfigError("repository '%s' is not in the manifest" % repo_id)
        ent = repos[repo_id]
        self.repo = repo_id
        self.url = ent.get("url")
        self.pin = ent.get("commit", "")
        if not self.url:
            raise ConfigError("manifest entry '%s' has no url" % repo_id)
        if not re.fullmatch(r"[0-9a-f]{40}", self.pin or ""):
            raise ConfigError("manifest entry '%s' has no full 40-hex commit pin (got %r)"
                              % (repo_id, self.pin))
        self.upstream = os.path.join(ROOT, "upstream", repo_id)
        self.work = os.path.join(ROOT, "work", repo_id)
        self.patch_dir = os.path.join(ROOT, "patches", repo_id)
        self.series_path = os.path.join(self.patch_dir, "series")
        self.baseline_path = os.path.join(ROOT, "work", ".%s.baseline.json" % repo_id)

    def series(self):
        """Ordered [(name, sha256)].  An absent series is a config error, not empty."""
        if not os.path.isfile(self.series_path):
            raise ConfigError("missing series file %s (an absent series is not an empty one)"
                              % self.series_path)
        out, seen = [], set()
        for raw in open(self.series_path, encoding="utf-8").read().splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if "/" in line or not line.endswith(".patch"):
                raise ConfigError("bad series entry %r" % line)
            if line in seen:
                raise ConfigError("series lists %s twice" % line)
            seen.add(line)
            p = os.path.join(self.patch_dir, line)
            if not os.path.isfile(p):
                raise ConfigError("series lists %s but the file is missing" % line)
            out.append((line, sha256_file(p)))
        return out

    def inputs(self):
        return {"repo": self.repo, "url": self.url, "pin": self.pin,
                "series": [{"name": n, "sha256": d} for n, d in self.series()]}

    def baseline(self):
        if not os.path.isfile(self.baseline_path):
            return None
        return json.load(open(self.baseline_path))


def stale_reasons(cfg, base):
    """Differences between the recorded baseline identity and current inputs."""
    if base is None:
        return ["no recorded baseline identity (%s)" % os.path.relpath(cfg.baseline_path, ROOT)]
    cur = cfg.inputs()
    r = []
    if base.get("pin") != cur["pin"]:
        r.append("pin changed: baseline %s, manifest %s" % (base.get("pin"), cur["pin"]))
    bn = [s["name"] for s in base.get("series", [])]
    cn = [s["name"] for s in cur["series"]]
    if bn != cn:
        r.append("series order/content changed: baseline %s, now %s" % (bn, cn))
    bd = {s["name"]: s["sha256"] for s in base.get("series", [])}
    for s in cur["series"]:
        if s["name"] in bd and bd[s["name"]] != s["sha256"]:
            r.append("patch %s edited in place (digest %s.. -> %s..)"
                     % (s["name"], bd[s["name"]][:12], s["sha256"][:12]))
    return r


# ------------------------------------------------------------------ upstream

def check_upstream(cfg, create=False):
    up = cfg.upstream
    if not os.path.isdir(os.path.join(up, ".git")):
        if os.path.exists(up) and os.listdir(up):
            raise Refused("upstream/%s exists but is not a git checkout" % cfg.repo)
        if not create:
            raise ConfigError("upstream/%s is missing; run scripts/bootstrap.sh" % cfg.repo)
        print("cloning %s into upstream/%s" % (cfg.url, cfg.repo))
        os.makedirs(os.path.dirname(up), exist_ok=True)
        tmp = tempfile.mkdtemp(prefix=".%s.clone-" % cfg.repo, dir=os.path.dirname(up))
        try:
            git(None, "clone", "-q", "--no-checkout", cfg.url, tmp)
            if not has_object(tmp, cfg.pin):
                raise Refused("pinned commit %s not found in %s" % (cfg.pin, cfg.url))
            git(tmp, "-c", "advice.detachedHead=false", "checkout", "-q", "--detach", cfg.pin)
            if os.path.exists(up):
                os.rmdir(up)
            os.rename(tmp, up)
        except Exception:
            shutil.rmtree(tmp, ignore_errors=True)
            raise
        # Read-only so that nothing edits the reference checkout by accident.
        subprocess.run(["chmod", "-R", "a-w", up], check=True)
    head = gout(up, "rev-parse", "HEAD")
    if head != cfg.pin:
        raise Refused("upstream/%s HEAD is %s, manifest pin is %s" % (cfg.repo, head, cfg.pin))
    st = gout(up, "status", "--porcelain=v1", "--ignored", "--untracked-files=all")
    if st:
        raise Refused("upstream/%s is not clean:\n%s" % (cfg.repo, st))
    return up


# ------------------------------------------------------------ reconstruction

def materialize(cfg, dest, branch=DEFAULT_BRANCH, series=None):
    """Clone the pinned upstream into dest and apply the series, one commit per
    patch.  Stops on the first conflict.  Returns (head_commit, tree)."""
    series = cfg.series() if series is None else series
    git(None, "clone", "-q", "--no-hardlinks", "--no-checkout", "-o", REMOTE,
        cfg.upstream, dest)
    git(dest, "-c", "advice.detachedHead=false", "checkout", "-q", "-b", branch, cfg.pin)
    for name, digest in series:
        p = os.path.join(cfg.patch_dir, name)
        if sha256_file(p) != digest:
            raise Refused("%s changed while reconstructing" % name)
        r = git(dest, "apply", "--index", "--whitespace=nowarn", p, check=False)
        if r.returncode != 0:
            raise Refused("patch %s does not apply (stopped, nothing recorded):\n%s"
                          % (name, r.stderr.strip()))
        git(dest, *GIT_ID, "commit", "-q", "--allow-empty", "--no-verify", "-m",
            "%s\n\nApplied by patchtool from patches/%s/%s (sha256 %s)."
            % (name, cfg.repo, name, digest))
    return gout(dest, "rev-parse", "HEAD"), gout(dest, "rev-parse", "HEAD^{tree}")


def ls_tree(repo, rev):
    """{path: (mode, blob)} for every tracked entry of rev."""
    out = git(repo, "ls-tree", "-r", "-z", "--full-tree", rev).stdout
    res = {}
    for ent in out.split("\0"):
        if not ent:
            continue
        meta, path = ent.split("\t", 1)
        mode, _typ, sha = meta.split()
        res[path] = (mode, sha)
    return res


def diff_trees(a, b):
    out = []
    for p in sorted(set(a) | set(b)):
        if p not in a:
            out.append("only in work saved tree: %s" % p)
        elif p not in b:
            out.append("missing from work saved tree: %s" % p)
        elif a[p] != b[p]:
            what = "mode %s -> %s" % (a[p][0], b[p][0]) if a[p][0] != b[p][0] else "content"
            out.append("differs (%s): %s" % (what, p))
    return out


def fs_manifest(root, include_git=False, include_dirs=False):
    """Independent filesystem view: path -> ('f', exec, sha256) | ('l', target) | ('d', mode)."""
    res = {}
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root)
        if not include_git and ".git" in dirnames and rel_dir == ".":
            dirnames.remove(".git")
        for d in list(dirnames):
            full = os.path.join(dirpath, d)
            rel = os.path.normpath(os.path.join(rel_dir, d))
            if os.path.islink(full):
                dirnames.remove(d)
                res[rel] = ("l", os.readlink(full))
            elif include_dirs:
                res[rel] = ("d", stat.S_IMODE(os.lstat(full).st_mode))
        for f in filenames:
            full = os.path.join(dirpath, f)
            rel = os.path.normpath(os.path.join(rel_dir, f))
            st = os.lstat(full)
            if stat.S_ISLNK(st.st_mode):
                res[rel] = ("l", os.readlink(full))
            elif include_dirs:
                res[rel] = ("f", stat.S_IMODE(st.st_mode), sha256_file(full))
            else:
                res[rel] = ("f", bool(st.st_mode & stat.S_IXUSR), sha256_file(full))
    return res


def diff_fs(expected, actual):
    out = []
    for p in sorted(set(expected) | set(actual)):
        if p not in actual:
            out.append("deleted locally: %s" % p)
        elif p not in expected:
            out.append("extra local file: %s" % p)
        elif expected[p] != actual[p]:
            e, a = expected[p], actual[p]
            if e[0] != a[0]:
                kind = "type"
            elif e[0] == "l":
                kind = "symlink target"
            elif e[1] != a[1]:
                kind = "executable bit"
            else:
                kind = "content"
            out.append("modified locally (%s): %s" % (kind, p))
    return out


# --------------------------------------------------------- work inspection

def porcelain(work):
    """[(XY, path)] including untracked and ignored files."""
    out = git(work, "status", "--porcelain=v1", "-z", "--ignored",
              "--untracked-files=all").stdout
    items, parts, i = [], out.split("\0"), 0
    while i < len(parts):
        ent = parts[i]
        i += 1
        if not ent:
            continue
        xy, path = ent[:2], ent[3:]
        if xy[0] in "RC":
            i += 1  # skip rename source
        items.append((xy, path))
    return items


def history_inventory(cfg, work, baseline_commit, branch):
    """Local Git state that recreation would lose.  Returns (blocking, notes, commits)
    where commits are all commits that a recovery copy must keep."""
    blocking, notes, commits = [], [], set()
    up = cfg.upstream

    def unique(sha):
        if baseline_commit and is_ancestor(work, sha, baseline_commit):
            return False
        return not has_object(up, sha)

    gd = os.path.join(work, ".git")
    if not os.path.isdir(gd):
        blocking.append(".git is not a directory (metadata outside the checkout)")
        return blocking, notes, commits
    if os.path.exists(os.path.join(gd, "objects", "info", "alternates")):
        blocking.append("repository uses alternates (objects outside the checkout)")
    wt = os.path.join(gd, "worktrees")
    if os.path.isdir(wt) and os.listdir(wt):
        blocking.append("linked worktrees exist: %s" % ", ".join(os.listdir(wt)))

    head_ref = git(work, "symbolic-ref", "-q", "HEAD", check=False).stdout.strip()
    head = gout(work, "rev-parse", "HEAD")
    commits.add(head)
    if head_ref != "refs/heads/" + branch:
        blocking.append("HEAD is %s, expected refs/heads/%s" % (head_ref or "detached", branch))
    if baseline_commit and head != baseline_commit:
        ahead = gout(work, "rev-list", "%s..HEAD" % baseline_commit).split()
        blocking.append("HEAD %s is not the saved baseline %s (%d unsaved commit(s)%s)"
                        % (head[:12], baseline_commit[:12], len(ahead),
                           "; baseline commit not an ancestor: amended or rewritten"
                           if not is_ancestor(work, baseline_commit, head) else ""))

    stashes = gout(work, "log", "-g", "--format=%H %gs", "refs/stash", check=False) \
        if git(work, "rev-parse", "-q", "--verify", "refs/stash", check=False).returncode == 0 else ""
    for line in stashes.splitlines():
        sha, msg = line.split(" ", 1)
        commits.add(sha)
        blocking.append("stash %s: %s" % (sha[:12], msg))

    refs = gout(work, "for-each-ref", "--format=%(refname) %(objectname) %(objecttype)")
    for line in refs.splitlines():
        name, sha, typ = line.split()
        if name == "refs/stash":
            continue
        target = gout(work, "rev-parse", sha + "^{}") if typ == "tag" else sha
        commits.add(target)
        if name == "refs/heads/" + branch:
            continue
        if name.startswith(("refs/remotes/%s/" % REMOTE, "refs/tags/", "refs/heads/")) \
                and not unique(target):
            notes.append("%s at %s (no unique commits: in upstream or baseline)" % (name, sha[:12]))
            continue
        blocking.append("ref %s at %s holds commits not in upstream or the saved baseline"
                        % (name, sha[:12]))

    logs_dir = os.path.join(gd, "logs")
    reflog_refs = ["HEAD"] if os.path.exists(os.path.join(logs_dir, "HEAD")) else []
    rh = os.path.join(logs_dir, "refs", "heads")
    for dp, _dn, fn in os.walk(rh):
        for f in fn:
            reflog_refs.append(os.path.relpath(os.path.join(dp, f), logs_dir).replace(os.sep, "/"))
    seen = set()
    for ref in reflog_refs:
        for sha in gout(work, "log", "-g", "--format=%H", ref, check=False).split():
            if sha in seen:
                continue
            seen.add(sha)
            commits.add(sha)
            if sha != head and unique(sha) and not is_ancestor(work, sha, head):
                blocking.append("prior state %s recoverable only via reflog %s" % (sha[:12], ref))

    fsck = git(work, "fsck", "--unreachable", "--no-progress", check=False).stdout
    unreach = re.findall(r"^unreachable commit ([0-9a-f]{40})", fsck, re.M)
    commits.update(unreach)
    # Objects copied from upstream (other upstream branches) are not local work.
    known = set()
    if unreach:
        chk = git(up, "cat-file", "--batch-check", inp=("\n".join(unreach) + "\n").encode(),
                  binary=True).stdout.decode()
        known = {l.split()[0] for l in chk.splitlines() if l.endswith(" commit") or " commit " in l}
    local = [c for c in unreach if c not in known]
    for c in local:
        blocking.append("unreachable commit %s (e.g. a dropped stash or rewritten commit)" % c[:12])
    if len(unreach) > len(local):
        notes.append("%d unreachable commits also exist in upstream (not local work)"
                     % (len(unreach) - len(local)))
    return blocking, notes, commits


def classify_local(work):
    staged, unstaged, untracked, ignored = [], [], [], []
    for xy, path in porcelain(work):
        if xy == "??":
            untracked.append(path)
        elif xy == "!!":
            ignored.append(path)
        else:
            if xy[0] not in " ?":
                staged.append("%s %s" % (xy[0], path))
            if xy[1] not in " ?":
                unstaged.append("%s %s" % (xy[1], path))
    return staged, unstaged, untracked, ignored


class Assessment:
    """Everything needed to decide whether work/ may be replaced."""

    def __init__(self, cfg, reconstruct=True):
        self.cfg = cfg
        self.base = cfg.baseline()
        self.branch = (self.base or {}).get("branch", DEFAULT_BRANCH)
        self.stale = stale_reasons(cfg, self.base) if self.base else []
        self.exists = os.path.isdir(os.path.join(cfg.work, ".git"))
        self.recon_tree = None
        self.tree_diff = []
        self.fs_diff = []
        self.local = ([], [], [], [])
        self.blocking, self.notes, self.commits = [], [], set()
        if not self.exists:
            return
        w = cfg.work
        self.head = gout(w, "rev-parse", "HEAD")
        self.head_tree = gout(w, "rev-parse", "HEAD^{tree}")
        self.local = classify_local(w)
        bc = (self.base or {}).get("baseline_commit")
        self.blocking, self.notes, self.commits = history_inventory(cfg, w, bc, self.branch)
        if reconstruct:
            tmp = tempfile.mkdtemp(prefix="patchtool-recon-")
            try:
                d = os.path.join(tmp, "recon " + cfg.repo)  # a space on purpose
                _c, self.recon_tree = materialize(cfg, d, self.branch)
                self.tree_diff = diff_trees(ls_tree(d, "HEAD"), ls_tree(w, "HEAD"))
                self.fs_diff = diff_fs(fs_manifest(d), fs_manifest(w))
            finally:
                shutil.rmtree(tmp, ignore_errors=True)

    @property
    def equivalent(self):
        return self.recon_tree is not None and self.recon_tree == self.head_tree

    @property
    def dirty(self):
        return any(self.local)

    def safe_to_replace(self):
        return self.exists and self.equivalent and not self.dirty and not self.blocking

    def report(self, out=print):
        c = self.cfg
        out("repository:      %s (%s)" % (c.repo, c.url))
        out("pin:             %s" % c.pin)
        try:
            ser = c.series()
            out("series:          %s" % (", ".join(n for n, _ in ser) or "(empty)"))
        except ConfigError as ex:
            out("series:          ERROR %s" % ex)
        if self.base is None:
            out("baseline:        none recorded (%s)" % os.path.relpath(c.baseline_path, ROOT))
        else:
            out("baseline:        commit %s, recorded %s" % (self.base["baseline_commit"][:12],
                                                          self.base.get("created", "?")))
            out("baseline inputs: %s" % ("STALE" if self.stale else "match current manifest/series"))
            for r in self.stale:
                out("  - " + r)
        if not self.exists:
            out("work copy:       absent")
            return
        out("work HEAD:       %s on %s, tree %s" % (self.head[:12], self.branch, self.head_tree))
        if self.recon_tree is not None:
            out("reconstructed:   tree %s from pin + series" % self.recon_tree)
            out("source equivalence (saved tree vs reconstruction): %s"
                % ("EQUIVALENT" if self.equivalent else "MISMATCH"))
            for d in self.tree_diff[:50]:
                out("  - " + d)
        labels = ("staged", "unstaged", "untracked", "ignored (possibly valuable)")
        for lab, items in zip(labels, self.local):
            out("%s: %d" % (lab, len(items)))
            for it in items[:30]:
                out("  - " + it)
        if self.fs_diff:
            out("unsaved deltas vs reconstruction (files on disk): %d" % len(self.fs_diff))
            for d in self.fs_diff[:50]:
                out("  - " + d)
        out("local Git history needing preservation: %s"
            % ("none" if not self.blocking else len(self.blocking)))
        for b in self.blocking:
            out("  - " + b)
        for n in self.notes[:20]:
            out("  note: " + n)
        if len(self.notes) > 20:
            out("  note: ... %d more refs without unique commits" % (len(self.notes) - 20))


# --------------------------------------------------------------- recovery copy

def make_recovery_copy(cfg, assess, backup_dir):
    backup_dir = os.path.abspath(backup_dir)
    work = os.path.realpath(cfg.work)
    if os.path.realpath(backup_dir).startswith(work + os.sep) or os.path.realpath(backup_dir) == work:
        raise Refused("backup directory must be outside %s" % cfg.work)
    os.makedirs(backup_dir, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    dest = os.path.join(backup_dir, "%s-%s" % (cfg.repo, stamp))
    if os.path.exists(dest):
        raise Refused("backup target exists: %s" % dest)
    subprocess.run(["cp", "-a", cfg.work, dest], check=True)
    # Verify: identical files (including all of .git, so refs, reflogs and
    # stash history), and every inventoried commit actually resolvable.
    a = fs_manifest(cfg.work, include_git=True, include_dirs=True)
    b = fs_manifest(dest, include_git=True, include_dirs=True)
    if a != b:
        raise Refused("recovery copy differs from original: %s" % diff_fs(a, b)[:5])
    for sha in sorted(assess.commits):
        if git(dest, "cat-file", "-e", sha + "^{commit}", check=False).returncode != 0:
            raise Refused("commit %s not recoverable from the copy" % sha)
    if gout(dest, "for-each-ref") != gout(cfg.work, "for-each-ref"):
        raise Refused("refs differ in recovery copy")
    if gout(dest, "stash", "list", check=False) != gout(cfg.work, "stash", "list", check=False):
        raise Refused("stash list differs in recovery copy")
    r = git(dest, "fsck", "--no-progress", "--no-dangling", check=False)
    if r.returncode != 0:
        raise Refused("fsck of recovery copy failed: %s" % r.stderr.strip())
    print("recovery copy verified: %s (%d commits, %d files)" % (dest, len(assess.commits), len(b)))
    return dest


# ------------------------------------------------------------------ commands

def write_baseline(cfg, commit, tree, branch, inputs=None):
    data = dict(inputs or cfg.inputs())
    data.update({"tool_version": TOOL_VERSION, "branch": branch, "baseline_commit": commit,
                 "baseline_tree": tree,
                 "created": datetime.datetime.now().astimezone().isoformat(timespec="seconds")})
    write_atomic(cfg.baseline_path, (json.dumps(data, indent=2) + "\n").encode())


def build_work(cfg, branch):
    """Build a fresh work copy next to work/<repo>; returns the temp path."""
    parent = os.path.dirname(cfg.work)
    os.makedirs(parent, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix=".%s.build-" % cfg.repo, dir=parent)
    try:
        dest = os.path.join(tmp, cfg.repo)
        commit, tree = materialize(cfg, dest, branch)
        return tmp, dest, commit, tree
    except Exception:
        shutil.rmtree(tmp, ignore_errors=True)
        raise


def cmd_bootstrap(cfg, a):
    check_upstream(cfg, create=not a.verify)
    cfg.series()  # reports a missing series distinctly
    if a.verify:
        asm = Assessment(cfg)
        asm.report()
        ok = asm.exists and asm.base is not None and not asm.stale and asm.equivalent \
            and not asm.dirty and not asm.blocking
        if ok:
            print("RESULT: work/%s is synchronized with pin + series" % cfg.repo)
            return 0
        if asm.exists and asm.equivalent and not asm.stale and not asm.blocking:
            print("RESULT: saved tree equals pin + series%s%s" % (
                "; no recorded baseline (scripts/bootstrap.sh --adopt records one)"
                if asm.base is None else "",
                "; local unsaved changes present" if asm.dirty else ""))
            return 0 if asm.base is None and not asm.dirty else 1
        print("RESULT: work/%s is NOT synchronized (see above)" % cfg.repo)
        return 1

    if a.adopt:
        asm = Assessment(cfg)
        asm.report()
        if not asm.exists:
            raise Refused("no work copy to adopt")
        if asm.base is not None and not asm.stale:
            print("baseline already recorded and current")
            return 0
        if not asm.equivalent or asm.dirty or asm.blocking:
            raise Refused("adopt requires a clean work copy whose saved tree equals the "
                          "reconstruction and no unpreserved history")
        write_baseline(cfg, asm.head, asm.head_tree, asm.branch)
        print("recorded baseline for existing work/%s at %s" % (cfg.repo, asm.head[:12]))
        return 0

    if not os.path.exists(cfg.work):
        tmp, dest, commit, tree = build_work(cfg, a.branch)
        os.rename(dest, cfg.work)
        os.rmdir(tmp)
        write_baseline(cfg, commit, tree, a.branch)
        print("created work/%s at %s (tree %s), %d patch(es)"
              % (cfg.repo, commit[:12], tree, len(cfg.series())))
        return 0

    asm = Assessment(cfg)
    if not a.recreate:
        asm.report()
        if asm.base is not None and not asm.stale and asm.equivalent and not asm.dirty \
                and not asm.blocking:
            print("RESULT: work/%s exists and is current; nothing to do" % cfg.repo)
            return 0
        print("RESULT: work/%s exists and is not current; nothing changed. Use --recreate "
              "[--backup-dir DIR] to rebuild it" % cfg.repo)
        return 1

    # Destructive path.  Build the replacement first: a conflict stops here
    # with the existing copy untouched.
    asm.report()
    branch = asm.branch if asm.base else a.branch
    tmp, dest, commit, tree = build_work(cfg, branch)
    try:
        if asm.safe_to_replace():
            print("source equivalence and history preservation established; no copy needed")
        elif a.backup_dir:
            make_recovery_copy(cfg, asm, a.backup_dir)
        else:
            raise Refused("refusing to recreate work/%s: %s. Save the work, or pass "
                          "--backup-dir DIR (outside work/) for a verified recovery copy."
                          % (cfg.repo, "; ".join(filter(None, [
                              "saved tree differs from pin + series" if not asm.equivalent else "",
                              "local unsaved files" if asm.dirty else "",
                              "%d history item(s)" % len(asm.blocking) if asm.blocking else ""]))))
        fail_point("recreate-before-swap")
    except BaseException:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
    old = os.path.join(tmp, "old-" + cfg.repo)
    os.rename(cfg.work, old)
    try:
        os.rename(dest, cfg.work)
    except BaseException:
        os.rename(old, cfg.work)
        raise
    write_baseline(cfg, commit, tree, branch)
    shutil.rmtree(tmp)
    print("recreated work/%s at %s (tree %s)" % (cfg.repo, commit[:12], tree))
    return 0


HEADER_FIELDS = [  # (key, header label, required)
    ("title", "Title", True), ("author", "Author", True), ("problem", "Problem", True),
    ("solution", "Solution", True), ("platform_scope", "Platform-Scope", True),
    ("upstream_status", "Upstream-Status", True), ("upstream_link", "Upstream-Link", False),
    ("verification", "Verification", True), ("report", "Report", False),
    ("removal_condition", "Removal-Condition", True),
]


def load_fields(a):
    fields = {}
    if a.header:
        txt = open(a.header, encoding="utf-8").read()
        if a.header.endswith(".json"):
            fields.update({k.replace("-", "_"): v for k, v in json.loads(txt).items()})
        else:
            key = None
            for line in txt.splitlines():
                m = re.match(r"^([A-Za-z][A-Za-z_-]*):\s?(.*)$", line)
                if m and not line.startswith((" ", "\t")):
                    key = m.group(1).lower().replace("-", "_")
                    fields[key] = m.group(2)
                elif key:
                    fields[key] = (fields[key] + "\n" + line.strip()).strip("\n")
    for k, _l, _r in HEADER_FIELDS:
        v = getattr(a, k, None)
        if v:
            fields[k] = v
    if a.date:
        fields["date"] = a.date
    missing = [l for k, l, r in HEADER_FIELDS if r and not str(fields.get(k, "")).strip()]
    if missing:
        raise Refused("missing required header field(s): %s" % ", ".join(missing))
    if fields["upstream_status"] not in STATUSES:
        raise Refused("upstream status must be one of %s" % ", ".join(STATUSES))
    if fields["upstream_status"] in ("submitted", "accepted") and not fields.get("upstream_link"):
        raise Refused("upstream status %s requires --upstream-link" % fields["upstream_status"])
    return fields


def format_header(cfg, fields, series):
    def emit(label, value):
        lines = str(value).strip().splitlines() or [""]
        if len(lines) == 1:
            return ["%s: %s" % (label, lines[0])]
        # Continuation lines are indented, so nothing in the header can be
        # taken by git apply for a diff line.
        return ["%s:" % label] + ["  " + l if l.strip() else "" for l in lines]
    date = fields.get("date") or datetime.date.today().isoformat()
    out = []
    out += emit("Title", fields["title"])
    out += emit("Repository", "%s (%s)" % (cfg.repo, cfg.url))
    out += emit("Date", date)
    out += emit("Author", fields["author"])
    out += emit("Base-Commit", cfg.pin)
    out += emit("Preceding-Patches", "\n".join(n for n, _ in series) or "none")
    for k, label, _r in HEADER_FIELDS[2:]:
        if fields.get(k):
            out += emit(label, fields[k])
    return "\n".join(out) + "\n\n"


def slugify(s):
    s = re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")
    return s[:50].strip("-") or "change"


def next_number(cfg, series):
    nums = [0]
    for n in os.listdir(cfg.patch_dir):
        m = re.match(r"^(\d{4})-", n)
        if m:
            nums.append(int(m.group(1)))
    for n, _ in series:
        m = re.match(r"^(\d{4})-", n)
        if m:
            nums.append(int(m.group(1)))
    return max(nums) + 1


def cmd_save(cfg, a):
    w = cfg.work
    if not os.path.isdir(os.path.join(w, ".git")):
        raise ConfigError("no work copy; run scripts/bootstrap.sh")
    base = cfg.baseline()
    if base is None:
        raise Refused("no recorded baseline; run scripts/bootstrap.sh --verify / --adopt first")
    st = stale_reasons(cfg, base)
    if st:
        raise Refused("work baseline is stale, saving would not reproduce:\n  " + "\n  ".join(st))
    branch = base.get("branch", DEFAULT_BRANCH)
    head = gout(w, "rev-parse", "HEAD")
    if gout(w, "symbolic-ref", "-q", "HEAD", check=False) != "refs/heads/" + branch:
        raise Refused("work is not on branch %s" % branch)
    if head != base["baseline_commit"]:
        raise Refused("HEAD %s is not the saved baseline %s (local commits or amend); "
                      "save-patch takes changes from the working tree only"
                      % (head[:12], base["baseline_commit"][:12]))
    fields = load_fields(a)
    series = cfg.series()

    # Build the selection in a private index: the user's index and files are
    # not touched until everything is written.
    gd = gout(w, "rev-parse", "--absolute-git-dir")
    tmpdir = tempfile.mkdtemp(prefix="patchtool-save-")
    try:
        idx = os.path.join(tmpdir, "index")
        env = {"GIT_INDEX_FILE": idx}
        if a.staged:
            shutil.copy2(os.path.join(gd, "index"), idx)
        else:
            if not a.paths:
                raise Refused("select changes: give paths (relative to work/%s) or --staged"
                              % cfg.repo)
            git(w, "read-tree", "HEAD", env=env)
            paths = [os.path.relpath(os.path.abspath(p), w) if os.path.isabs(p) else p
                     for p in a.paths]
            r = git(w, "add", "-A", "--", *paths, env=env, check=False)
            if r.returncode != 0:
                raise Refused("cannot select paths: %s" % r.stderr.strip())
        new_tree = gout(w, "write-tree", env=env)
        raw = git(w, "diff-index", "--cached", "-z", "--no-renames", "HEAD", env=env).stdout
        if not raw.strip("\0"):
            raise Refused("selected changes are empty; nothing saved")
        for rec in raw.split(":")[1:]:
            modes = rec.split()[:2]
            if "160000" in modes:
                raise Refused("submodule (gitlink) changes are not supported; nothing saved")
        diff = git(w, "diff", "--cached", "--binary", "--full-index", "--no-renames",
                   "--no-ext-diff", "--no-textconv", "--src-prefix=a/", "--dst-prefix=b/",
                   "HEAD", env=env, binary=True).stdout
        if b"Binary files " in diff and b"GIT binary patch" not in diff:
            raise Refused("diff contains a non-reproducible binary notice; nothing saved")
        # Round-trip proof before mutation: the patch applied to HEAD must give
        # exactly the selected tree (bytes, modes, symlinks).
        idx2 = os.path.join(tmpdir, "index2")
        git(w, "read-tree", "HEAD", env={"GIT_INDEX_FILE": idx2})
        pf = os.path.join(tmpdir, "check.patch")
        open(pf, "wb").write(diff)
        r = git(w, "apply", "--cached", "--whitespace=nowarn", pf,
                env={"GIT_INDEX_FILE": idx2}, check=False)
        if r.returncode != 0 or gout(w, "write-tree", env={"GIT_INDEX_FILE": idx2}) != new_tree:
            raise Refused("patch does not round-trip (unsupported change?): %s; nothing saved"
                          % r.stderr.strip())

        num = next_number(cfg, series)
        name = "%04d-%s.patch" % (num, slugify(a.name or fields["title"]))
        path = os.path.join(cfg.patch_dir, name)
        content = format_header(cfg, fields, series).encode() + diff
        old_series = open(cfg.series_path, "rb").read()
        created = series_written = False
        ref_old = head
        moved = False
        try:
            with open(path, "xb") as f:  # never overwrite an existing number
                created = True
                f.write(content)
            fail_point("after-patch-file")
            ns = old_series
            if ns and not ns.endswith(b"\n"):
                ns += b"\n"
            write_atomic(cfg.series_path, ns + name.encode() + b"\n")
            series_written = True
            fail_point("after-series")
            commit = gout(w, *GIT_ID, "commit-tree", new_tree, "-p", head, "-m",
                          "%s\n\n%s" % (name, fields["title"]))
            git(w, "update-ref", "-m", "patchtool save %s" % name,
                "refs/heads/" + branch, commit, ref_old)
            moved = True
            fail_point("after-commit")
            write_baseline(cfg, commit, new_tree, branch)
        except BaseException:
            if moved:
                git(w, "update-ref", "-m", "patchtool rollback", "refs/heads/" + branch,
                    ref_old, check=False)
            if series_written:
                write_atomic(cfg.series_path, old_series)
            if created and os.path.exists(path):
                os.remove(path)
            print("save failed; series, patch directory and baseline restored, "
                  "working-tree edits untouched", file=sys.stderr)
            raise
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)
    # Bring the real index in line with the new HEAD for the saved paths.
    if a.staged:
        pass
    else:
        git(w, "reset", "-q", "--", *paths, check=False)
    print("saved patches/%s/%s (%d bytes); series and baseline advanced to %s"
          % (cfg.repo, name, len(content), commit[:12]))
    return 0


def cmd_reproduce(cfg, a):
    check_upstream(cfg)
    series = cfg.series()
    tmp = tempfile.mkdtemp(prefix="patchtool-repro-")
    try:
        dest = os.path.join(a.out or tmp, "repro " + cfg.repo)
        if a.out and os.path.exists(dest):
            raise Refused("%s exists" % dest)
        commit, tree = materialize(cfg, dest)
        print("reconstructed %s from pin %s + %d patch(es): tree %s"
              % (dest if a.out else "(temp)", cfg.pin[:12], len(series), tree))
        rc = 0
        base = cfg.baseline()
        if base is not None:
            st = stale_reasons(cfg, base)
            print("recorded baseline inputs: %s" % ("STALE" if st else "match"))
            for r in st:
                print("  - " + r)
            if st:
                rc = 1
        if not os.path.isdir(os.path.join(cfg.work, ".git")):
            print("no work copy to compare")
            return 1 if a.source_only else rc
        wt = gout(cfg.work, "rev-parse", "HEAD^{tree}")
        td = diff_trees(ls_tree(dest, "HEAD"), ls_tree(cfg.work, "HEAD"))
        print("work saved tree (HEAD) %s: %s" % (wt, "EQUIVALENT" if wt == tree and not td
                                                else "MISMATCH"))
        for d in td:
            print("  - " + d)
        if wt != tree:
            rc = 1
        if base is not None and base.get("baseline_commit") != gout(cfg.work, "rev-parse", "HEAD"):
            print("work HEAD is not the recorded baseline commit (unsaved/amended commits)")
            rc = 1
        fsd = diff_fs(fs_manifest(dest), fs_manifest(cfg.work))
        staged = [s for s in classify_local(cfg.work)[0]]
        print("unsaved deltas in work (reported, not discarded): %d file(s), %d staged entry(ies)"
              % (len(fsd), len(staged)))
        for d in fsd:
            print("  - " + d)
        for s in staged:
            print("  - staged " + s)
        if a.strict and (fsd or staged):
            rc = 1
        if a.source_only:
            # Only the source decides: the committed tree equals pin + series
            # and nothing unsaved or staged sits on top of it. A stale
            # baseline record (e.g. a patch header reworded in place) or a
            # HEAD other than the baseline commit is reported above, but the
            # files a build compiles are still exactly pin + series.
            rc = 0 if wt == tree and not td and not fsd and not staged else 1
            print("source check: %s" % ("work equals pin + series" if rc == 0
                                         else "work DIFFERS from pin + series"))
        return rc
    finally:
        if not a.out:
            shutil.rmtree(tmp, ignore_errors=True)


def cmd_status(cfg, a):
    asm = Assessment(cfg, reconstruct=a.reconstruct)
    asm.report()
    if not asm.exists:
        return 1
    w = cfg.work
    print("branches:")
    print(gout(w, "branch", "-vv", "--no-color"))
    if not a.reconstruct:
        print("(source equivalence not checked; pass --reconstruct or run scripts/reproduce)")
    return 0 if not (asm.stale or asm.dirty or asm.blocking or asm.base is None) else 1


def main(argv=None):
    p = argparse.ArgumentParser(prog="patchtool")
    sub = p.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("bootstrap")
    b.add_argument("--repo")
    m = b.add_mutually_exclusive_group()
    m.add_argument("--verify", action="store_true", help="read-only check")
    m.add_argument("--recreate", action="store_true", help="rebuild work after preservation checks")
    m.add_argument("--adopt", action="store_true", help="record a baseline for an existing equivalent work copy")
    b.add_argument("--backup-dir", help="verified recovery copy location for --recreate")
    b.add_argument("--branch", default=DEFAULT_BRANCH)
    s = sub.add_parser("save-patch")
    s.add_argument("--repo")
    s.add_argument("--header", help="file with 'Key: value' lines (indented continuation) or .json")
    s.add_argument("--name", help="slug for the file name (default from title)")
    s.add_argument("--date")
    for k, label, _r in HEADER_FIELDS:
        s.add_argument("--" + k.replace("_", "-"), dest=k)
    s.add_argument("--staged", action="store_true", help="save exactly the staged index")
    s.add_argument("paths", nargs="*")
    r = sub.add_parser("reproduce")
    r.add_argument("--repo")
    r.add_argument("--out", help="keep the reconstruction in this directory")
    r.add_argument("--strict", action="store_true", help="fail on unsaved deltas too")
    r.add_argument("--source-only", action="store_true",
                   help="exit 0 exactly when work equals pin + series with no unsaved or staged "
                        "changes; baseline record staleness is reported but not counted")
    t = sub.add_parser("status")
    t.add_argument("--repo")
    t.add_argument("--reconstruct", action="store_true")
    a = p.parse_args(argv)
    try:
        cfg = Config(a.repo)
        return {"bootstrap": cmd_bootstrap, "save-patch": cmd_save,
                "reproduce": cmd_reproduce, "status": cmd_status}[a.cmd](cfg, a)
    except ConfigError as ex:
        print("CONFIG ERROR: %s" % ex, file=sys.stderr)
        return 2
    except Refused as ex:
        print("REFUSED: %s" % ex, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
