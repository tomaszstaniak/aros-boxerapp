#!/usr/bin/env python3
"""Host tests for packaging/Install-Assign:
python3 tests/build-tooling/test_install_assign.py

The script is run by a small interpreter for the subset of the AmigaDOS
Shell it uses (If/Else/EndIf, Set, Echo with redirection, Delete, Rename,
Copy, List LFORMAT, Search, Assign, CD, Quit) over a scratch directory that
stands for the volumes. It models what the script relies on, not the whole
Shell: Rename refuses an existing target, as AROS DOS does. A run can be
stopped after any number of commands, as a reset would. Runs on AROS are
still needed for the real Shell's behaviour."""
import os, re, shutil, sys, tempfile, unittest


def write_text(path, data):
    with open(path, "w") as f:
        f.write(data)


def append_text(path, data):
    with open(path, "a") as f:
        f.write(data)


def read_text(path):
    with open(path) as f:
        return f.read()

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "packaging", "Install-Assign")
SCRIPT = os.environ.get("INSTALL_ASSIGN_SCRIPT", SCRIPT)


class Stopped(Exception):
    pass


class Shell:
    def __init__(self, root, answer="2"):
        self.root = root
        self.assigns = {}
        self.cwd = "Work:Boxer"
        self.answer = answer
        self.vars = {}
        self.rc = 0

    # --- paths -------------------------------------------------------
    def host(self, path):
        if ":" not in path:
            base = self.cwd
            path = base + ("" if base.endswith(":") else "/") + path
        vol, rest = path.split(":", 1)
        if vol + ":" in self.assigns:
            return os.path.join(self.host(self.assigns[vol + ":"]), rest) if rest else self.host(self.assigns[vol + ":"])
        return os.path.join(self.root, vol, rest)

    def exists(self, path):
        return os.path.exists(self.host(path))

    # --- parsing ------------------------------------------------------
    @staticmethod
    def strip_comment(line):
        q = False
        for i, ch in enumerate(line):
            if ch == '"' and (i == 0 or line[i - 1] != "*"):
                q = not q
            elif ch == ";" and not q:
                return line[:i]
        return line

    def substitute(self, line):
        def var(m):
            name = m.group(1) or m.group(2)
            return self.vars.get(name, m.group(0))
        line = re.sub(r"\$\{(\w+)\}|\$(\w+)", var, line)
        def back(m):
            return self.capture(m.group(1)).rstrip("\n")
        return re.sub(r"`([^`]*)`", back, line)

    @staticmethod
    def tokens(line):
        out, i, n = [], 0, len(line)
        while i < n:
            if line[i].isspace():
                i += 1
                continue
            redir = ""
            if line[i] == ">":
                redir = ">>" if line.startswith(">>", i) else ">"
                i += len(redir)
            if i < n and line[i] == '"':
                i += 1
                s = ""
                while i < n and line[i] != '"':
                    if line[i] == "*" and i + 1 < n:
                        nxt = line[i + 1]
                        s += {'"': '"', "N": "\n", "n": "\n", "*": "*"}.get(nxt, "*" + nxt)
                        i += 2
                    else:
                        s += line[i]
                        i += 1
                i += 1
            else:
                j = i
                while j < n and not line[j].isspace():
                    j += 1
                s = line[i:j]
                i = j
            out.append((redir, s))
        return out

    # --- commands -----------------------------------------------------
    def capture(self, cmd):
        out = []
        self.command(cmd, out)
        return "".join(out)

    def command(self, line, out):
        toks = self.tokens(line)
        redir = [t for t in toks if t[0]]
        args = [s for r, s in toks if not r]
        buf = []
        rc = self.run(args, buf)
        text = "".join(buf)
        if redir:
            mode, target = redir[-1]
            if target.upper() != "NIL:":
                p = self.host(target)
                if not os.path.isdir(os.path.dirname(p)):
                    rc = max(rc, 20)
                else:
                    with open(p, "a" if mode == ">>" else "w") as f:
                        f.write(text)
        else:
            out.append(text)
        self.rc = rc

    def run(self, a, out):
        c = a[0].lower()
        kw = [x.upper() for x in a[1:]]
        if c == "failat":
            return 0
        if c == "set":
            self.vars[a[1]] = " ".join(a[2:])
            return 0
        if c == "unset":
            self.vars.pop(a[1], None)
            return 0
        if c == "echo":
            out.append(" ".join(a[1:]) + "\n")
            return 0
        if c == "cd":
            if len(a) == 1:
                out.append(self.cwd + "\n")
                return 0
            target = a[1]
            if not os.path.isdir(self.host(target)):
                return 20
            if ":" not in target:
                target = self.cwd + ("" if self.cwd.endswith(":") else "/") + target
            if target.endswith(":") and target in self.assigns:
                target = self.assigns[target]
            self.cwd = target
            return 0
        if c == "assign":
            name = a[1]
            if "EXISTS" in kw:
                if name in self.assigns:
                    out.append(name + " " + self.assigns[name] + "\n")
                    return 0
                return 5
            if "REMOVE" in kw:
                self.assigns.pop(name, None)
                return 0
            self.assigns[name] = a[2]
            return 0
        if c == "requestchoice":
            out.append(self.answer + "\n")
            return 0
        if c == "delete":
            p = self.host(a[1])
            if not os.path.exists(p):
                return 20
            shutil.rmtree(p) if os.path.isdir(p) else os.unlink(p)
            return 0
        if c == "rename":
            src, dst = self.host(a[1]), self.host(a[2])
            if not os.path.exists(src) or os.path.exists(dst):
                return 10
            os.rename(src, dst)
            return 0
        if c == "copy":
            src = self.host(a[1])
            dst = self.host(a[3] if a[2].upper() == "TO" else a[2])
            if not os.path.isfile(src):
                return 20
            shutil.copyfile(src, dst)
            return 0
        if c == "list":
            p = self.host(a[1])
            if not os.path.exists(p):
                return 20
            out.append(str(os.path.getsize(p)) + "\n")
            return 0
        if c == "search":
            p = self.host(a[1])
            if not os.path.isfile(p):
                return 20
            with open(p) as f:
                data = f.read().lower()
            return 0 if a[2].lower() in data else 5
        raise AssertionError("command not modelled: " + " ".join(a))

    def condition(self, args):
        neg = False
        if args and args[0].upper() == "NOT":
            neg, args = True, args[1:]
        if args[0].upper() == "WARN":
            r = self.rc >= 5
        elif args[0].upper() == "EXISTS":
            r = self.exists(args[1])
        elif len(args) == 3 and args[1].upper() == "EQ":
            r = args[0].lower() == args[2].lower()
        else:
            raise AssertionError("condition not modelled: %r" % args)
        return r != neg

    # --- script -------------------------------------------------------
    def execute(self, path, argline, stop_after=None):
        text = read_text(path)
        lines = text.split("\n")
        template = next(l for l in lines if l.startswith(".key")).split(None, 1)[1]
        params = self.bind(template, argline)
        keys = [item.split("/")[0].upper() for item in template.split(",")]
        def sub(m):
            # Only template keys are replaced; "${Var}" is a variable.
            if m.group(1).upper() not in keys:
                return m.group(0)
            return params.get(m.group(1).upper(), "")
        lines = [re.sub(r"\{(\w+)\}", sub, l) for l in lines]
        out, stack, executed = [], [], 0
        for raw in lines:
            line = self.strip_comment(raw).strip()
            if not line or line.startswith("."):
                continue
            word = line.split()[0].lower()
            active = all(stack)
            if word == "if":
                if active:
                    toks = [s for r, s in self.tokens(self.substitute(line[2:]))]
                    stack.append(self.condition(toks))
                else:
                    stack.append(None)          # inside a skipped block
                continue
            if word == "else":
                if stack[-1] is not None:
                    stack[-1] = not stack[-1]
                continue
            if word == "endif":
                stack.pop()
                continue
            if not active:
                continue
            if stop_after is not None and executed >= stop_after:
                raise Stopped()
            executed += 1
            if word == "quit":
                parts = line.split()
                return (int(parts[1]) if len(parts) > 1 else 0), "".join(out)
            self.command(self.substitute(line), out)
        return 0, "".join(out)

    @staticmethod
    def bind(template, argline):
        names = []
        for item in template.split(","):
            n, *flags = item.split("/")
            names.append((n.upper(), [f.upper() for f in flags]))
        params = {}
        toks = [s for r, s in Shell.tokens(argline)]
        i, positional = 0, [n for n, f in names if not f]
        while i < len(toks):
            t = toks[i].upper()
            hit = next(((n, f) for n, f in names if n == t), None)
            if hit and "S" in hit[1]:
                params[hit[0]] = hit[0]
                i += 1
            elif hit:
                params[hit[0]] = toks[i + 1]
                i += 2
            else:
                params[positional.pop(0)] = toks[i]
                i += 1
        return params


USER = "; my own startup\nAssign Games: Work:Games\n"


class InstallAssign(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="install assign ")
        for d in ("S", "Work/Boxer", "RAM"):
            os.makedirs(os.path.join(self.root, d))
        write_text(os.path.join(self.root, "Work/Boxer/BoxerUI"), "binary")

    def tearDown(self):
        shutil.rmtree(self.root)

    def path(self, rel):
        return os.path.join(self.root, rel)

    def read(self, rel):
        p = self.path(rel)
        return read_text(p) if os.path.exists(p) else None

    def write(self, rel, data):
        write_text(self.path(rel), data)

    def install(self, stop_after=None):
        sh = Shell(self.root)
        return sh.execute(SCRIPT, 'DIR "Work:Boxer"', stop_after)

    def files(self):
        return sorted(os.listdir(self.path("S")))

    def test_fresh_install_and_rerun(self):
        rc, out = self.install()
        self.assertEqual(rc, 0, out)
        su = self.read("S/User-Startup")
        self.assertIn(";BEGIN Boxer", su)
        self.assertIn(";END Boxer", su)
        self.assertIn('Assign Boxer: "Work:Boxer"', self.read("S/Boxer-Assign"))
        rc, out = self.install()
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.read("S/User-Startup"), su)
        self.assertEqual(self.files(), ["Boxer-Assign", "User-Startup"])

    def test_existing_lines_kept(self):
        self.write("S/User-Startup", USER)
        rc, out = self.install()
        self.assertEqual(rc, 0, out)
        su = self.read("S/User-Startup")
        self.assertTrue(su.startswith(USER))
        self.assertIn(";END Boxer", su)
        self.assertEqual(self.files(), ["Boxer-Assign", "User-Startup"])

    def test_interrupted_replacement_is_not_taken_for_a_fresh_install(self):
        # A run stopped between moving the startup file aside and renaming
        # the new one in: the user's commands are only in .boxer-old.
        self.write("S/User-Startup.boxer-old", USER)
        self.write("S/User-Startup.boxer-new", USER + "\n;BEGIN Boxer (Install-Assign)\n;END Boxer\n")
        rc, out = self.install()
        self.assertNotEqual(rc, 0, out)
        self.assertIsNone(self.read("S/User-Startup"))
        self.assertEqual(self.read("S/User-Startup.boxer-old"), USER)
        self.assertIn("User-Startup.boxer-old", out)
        # The user is told to keep and compare the files, not to delete them.
        self.assertIn("Keep a copy", out)
        self.assertIn("compare them with", out)
        self.assertNotIn("remove the others", out)

    def test_foreign_scratch_names_are_not_deleted(self):
        self.write("S/User-Startup", USER)
        self.write("S/User-Startup.boxer-old", "someone else's\n")
        self.write("S/Boxer-Assign.new", "someone else's too\n")
        rc, out = self.install()
        self.assertNotEqual(rc, 0, out)
        self.assertEqual(self.read("S/User-Startup.boxer-old"), "someone else's\n")
        self.assertEqual(self.read("S/Boxer-Assign.new"), "someone else's too\n")
        self.assertEqual(self.read("S/User-Startup"), USER)

    def test_begin_line_alone_is_not_an_installed_block(self):
        self.write("S/User-Startup", USER + ";BEGIN Boxer\n")
        rc, out = self.install()
        self.assertNotIn("already has the Boxer block", out)
        self.assertNotEqual(rc, 0, out)
        self.assertEqual(self.read("S/User-Startup"), USER + ";BEGIN Boxer\n")

    def test_reset_at_any_point_never_loses_user_commands(self):
        # A reset after each command in turn, then a normal run: the user's
        # lines are always in User-Startup or in a file the run names and
        # leaves alone; a run never reports success without them.
        steps = 0
        while True:
            self.tearDown()
            self.setUp()
            self.write("S/User-Startup", USER)
            try:
                self.install(stop_after=steps)
                break                       # finished without being stopped
            except Stopped:
                pass
            rc, out = self.install()
            su = self.read("S/User-Startup")
            if rc == 0:
                self.assertTrue(su is not None and su.startswith(USER), "reset after %d: %r" % (steps, su))
            else:
                kept = [self.read("S/" + n) for n in self.files()]
                self.assertTrue(any(k and k.startswith(USER) for k in kept), "reset after %d" % steps)
                if su is not None:
                    self.assertTrue(su.startswith(USER), "reset after %d: %r" % (steps, su))
            steps += 1
        self.assertGreater(steps, 20)


if __name__ == "__main__":
    unittest.main()
