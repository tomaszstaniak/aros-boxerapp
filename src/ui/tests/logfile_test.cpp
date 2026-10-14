// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Host unit test for src/ui/logfile.{h,cpp}: the name a log gets, that no
// existing file is replaced, and what happens when an explicit LOG file is
// open in another process.
// Usage: logfile_test <scratch dir>
#include "../logfile.h"

#include "../../model/fsutil.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace boxer_ui;
namespace fu = boxer::fsutil;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string get(const std::string &p) { std::string d; return fu::readFile(p, d) ? d : "<missing>"; }

// A model of AROS opening for writing: a path held open by a "process"
// cannot be opened by another (MODE_NEWFILE's exclusive lock); an opened
// file is restarted (truncated) and stays held. Every call is recorded,
// so the tests can see that nothing is checked before the open.
struct Opener {
    std::set<std::string> held;      // open in some process
    std::vector<std::string> calls;
    std::set<std::string> broken;    // cannot be opened at all
    LogOpener fn() {
        return [this](const std::string &p) {
            calls.push_back(p);
            if (broken.count(p)) return LogClaim::Failed;
            if (held.count(p)) return LogClaim::InUse;
            if (!fu::writeFile(p, "")) return LogClaim::Failed;
            held.insert(p);
            return LogClaim::Opened;
        };
    }
};

int main(int argc, char **argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: logfile_test <scratch dir>\n"); return 2; }
    const std::string scratch = argv[1];
    fu::removeTree(scratch);
    if (!fu::makeDirs(scratch)) { std::fprintf(stderr, "cannot create %s\n", scratch.c_str()); return 2; }
    Opener o;
    // Default: the drawer is made, the first process gets -1, the next -2;
    // each file is created, then opened (and held).
    const std::string logs = fu::join(scratch, "Boxer Logs");
    LogChoice a = chooseLogFile("", logs, o.fn());
    CHECK(a.path == fu::join(logs, "boxerui-1.log") && a.note.empty() && fu::isFile(a.path) && o.held.count(a.path));
    LogChoice b = chooseLogFile("", logs, o.fn());
    CHECK(b.path == fu::join(logs, "boxerui-2.log") && fu::isFile(b.path));
    // An existing file is never replaced or reused, whatever its number.
    fu::writeFile(fu::join(logs, "boxerui-3.log"), "an older run's log");
    fu::writeFile(a.path, "the first process's lines");
    LogChoice c = chooseLogFile("", logs, o.fn());
    CHECK(c.path == fu::join(logs, "boxerui-4.log"));
    CHECK(get(fu::join(logs, "boxerui-3.log")) == "an older run's log");
    CHECK(get(a.path) == "the first process's lines");
    // A gap left by a deleted log is used again; still nothing replaced.
    fu::removeTree(b.path);
    o.held.erase(b.path);
    CHECK(chooseLogFile("", logs, o.fn()).path == b.path);
    // Many processes in a row: all different files.
    std::set<std::string> seen;
    for (int i = 0; i < 20; ++i) seen.insert(chooseLogFile("", logs, o.fn()).path);
    CHECK(seen.size() == 20 && !seen.count(""));
    fu::writeFile(fu::join(logs, "notes.txt"), "x");
    CHECK(createNumberedLog(logs, "other", ".log") == fu::join(logs, "other-1.log"));

    // The drawer cannot be made (a file has its name): logged beside it.
    const std::string blocked = fu::join(scratch, "Blocked");
    fu::writeFile(blocked, "a file, not a drawer");
    LogChoice d = chooseLogFile("", blocked, o.fn());
    CHECK(d.path == fu::join(scratch, "boxerui-1.log") && !d.note.empty());
    CHECK(get(blocked) == "a file, not a drawer");

    // Explicit LOG, nobody has it open: opened as given, and restarted as
    // before. The open is the only call: nothing is checked first.
    fu::makeDirs(fu::join(scratch, "Work"));
    const std::string mine = fu::join(scratch, "Work/boxerui.log");
    fu::writeFile(mine, "previous run");
    o.calls.clear();
    LogChoice e = chooseLogFile(mine, logs, o.fn());
    CHECK(e.path == mine && e.note.empty() && get(mine).empty());
    CHECK((o.calls == std::vector<std::string>{mine}));
    // Two processes start with the same LOG at the same moment: the open of
    // one succeeds and holds the file; the other's open of the same name is
    // refused, so it takes a numbered file and says why. Nothing it does
    // touches the first one's file.
    fu::writeFile(mine, "first process's lines");
    o.calls.clear();
    LogChoice f = chooseLogFile(mine, logs, o.fn());
    CHECK(f.path == fu::join(scratch, "Work/boxerui-1.log") && fu::isFile(f.path));
    CHECK(f.note.find("open in another process") != std::string::npos && get(mine) == "first process's lines");
    CHECK((o.calls == std::vector<std::string>{mine, f.path}));
    LogChoice g = chooseLogFile(mine, logs, o.fn());
    CHECK(g.path == fu::join(scratch, "Work/boxerui-2.log"));
    // When the first process ends, its file is free again for the next.
    o.held.erase(mine);
    CHECK(chooseLogFile(mine, logs, o.fn()).path == mine);
    // The extension's case is kept; a name without one gets none.
    const std::string upper = fu::join(scratch, "Work/BOXER.LOG");
    o.held.insert(upper);
    CHECK(chooseLogFile(upper, logs, o.fn()).path == fu::join(scratch, "Work/BOXER-1.LOG"));
    const std::string bare = fu::join(scratch, "Work/trace");
    o.held.insert(bare);
    CHECK(chooseLogFile(bare, logs, o.fn()).path == fu::join(scratch, "Work/trace-1"));
    // In use, and the drawer cannot be written: no file, the note says so.
    const std::string nowhere = fu::join(scratch, "No Such Drawer/x.log");
    o.held.insert(nowhere);
    LogChoice h = chooseLogFile(nowhere, logs, o.fn());
    CHECK(h.path.empty() && h.note.find("no free name") != std::string::npos);
    // The explicit file cannot be opened at all (not in use): no log, said.
    const std::string bad = fu::join(scratch, "Work/bad.log");
    o.broken.insert(bad);
    LogChoice i = chooseLogFile(bad, logs, o.fn());
    CHECK(i.path.empty() && i.note.find("could not be opened") != std::string::npos);
    CHECK(chooseLogFile(mine, logs, LogOpener()).path.empty());

    if (failures) {
        std::printf("logfile_test: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    std::printf("logfile_test: all %d checks passed\n", checks);
    fu::removeTree(scratch);
    return 0;
}
