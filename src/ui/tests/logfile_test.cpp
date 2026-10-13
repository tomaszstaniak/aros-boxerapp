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

using namespace boxer_ui;
namespace fu = boxer::fsutil;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string get(const std::string &p) { std::string d; return fu::readFile(p, d) ? d : "<missing>"; }

int main(int argc, char **argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: logfile_test <scratch dir>\n"); return 2; }
    const std::string scratch = argv[1];
    fu::removeTree(scratch);
    if (!fu::makeDirs(scratch)) { std::fprintf(stderr, "cannot create %s\n", scratch.c_str()); return 2; }
    const auto never = [](const std::string &) { return false; };

    // Default: the drawer is made, the first process gets -1, the next -2.
    const std::string logs = fu::join(scratch, "Boxer Logs");
    LogChoice a = chooseLogFile("", logs, never);
    CHECK(a.path == fu::join(logs, "boxerui-1.log") && a.note.empty() && fu::isFile(a.path));
    LogChoice b = chooseLogFile("", logs, never);
    CHECK(b.path == fu::join(logs, "boxerui-2.log") && fu::isFile(b.path));
    // An existing file is never replaced or reused, whatever its number.
    fu::writeFile(fu::join(logs, "boxerui-3.log"), "an older run's log");
    fu::writeFile(a.path, "the first process's lines");
    LogChoice c = chooseLogFile("", logs, never);
    CHECK(c.path == fu::join(logs, "boxerui-4.log"));
    CHECK(get(fu::join(logs, "boxerui-3.log")) == "an older run's log");
    CHECK(get(a.path) == "the first process's lines");
    // A gap left by a deleted log is used again; still nothing replaced.
    fu::removeTree(b.path);
    CHECK(chooseLogFile("", logs, never).path == b.path);
    // Many processes in a row: all different files.
    std::set<std::string> seen;
    for (int i = 0; i < 20; ++i) seen.insert(chooseLogFile("", logs, never).path);
    CHECK(seen.size() == 20 && !seen.count(""));
    // Other names in the drawer do not count.
    fu::writeFile(fu::join(logs, "notes.txt"), "x");
    CHECK(createNumberedLog(logs, "other", ".log") == fu::join(logs, "other-1.log"));

    // The drawer cannot be made (a file has its name): logged beside it.
    const std::string blocked = fu::join(scratch, "Blocked");
    fu::writeFile(blocked, "a file, not a drawer");
    LogChoice d = chooseLogFile("", blocked, never);
    CHECK(d.path == fu::join(scratch, "boxerui-1.log") && !d.note.empty());
    CHECK(get(blocked) == "a file, not a drawer");

    // Explicit LOG, not in use: used as given; an existing file there is
    // restarted by the caller as before, so the choice does not touch it.
    fu::makeDirs(fu::join(scratch, "Work"));
    const std::string mine = fu::join(scratch, "Work/boxerui.log");
    fu::writeFile(mine, "previous run");
    LogChoice e = chooseLogFile(mine, logs, never);
    CHECK(e.path == mine && e.note.empty() && get(mine) == "previous run");
    // A file that does not exist yet: also as given.
    CHECK(chooseLogFile(fu::join(scratch, "Work/new.log"), logs, never).path == fu::join(scratch, "Work/new.log"));
    // In use by another process (a game started from an icon reads the
    // same LOG= ToolType): a numbered file beside it, the original kept.
    const auto busy = [&](const std::string &p) { return p == mine; };
    LogChoice f = chooseLogFile(mine, logs, busy);
    CHECK(f.path == fu::join(scratch, "Work/boxerui-1.log") && fu::isFile(f.path));
    CHECK(f.note.find("open in another process") != std::string::npos && get(mine) == "previous run");
    LogChoice g = chooseLogFile(mine, logs, busy);
    CHECK(g.path == fu::join(scratch, "Work/boxerui-2.log"));
    // The extension's case is kept; a name without one gets none.
    const std::string upper = fu::join(scratch, "Work/BOXER.LOG");
    CHECK(chooseLogFile(upper, logs, [&](const std::string &p) { return p == upper; }).path ==
          fu::join(scratch, "Work/BOXER-1.LOG"));
    const std::string bare = fu::join(scratch, "Work/trace");
    CHECK(chooseLogFile(bare, logs, [&](const std::string &p) { return p == bare; }).path ==
          fu::join(scratch, "Work/trace-1"));
    // A drawer that cannot be written: no file, and the note says so.
    LogChoice h = chooseLogFile(fu::join(scratch, "No Such Drawer/x.log"), logs, [](const std::string &) { return true; });
    CHECK(h.path.empty() && h.note.find("no free name") != std::string::npos);

    if (failures) {
        std::printf("logfile_test: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    std::printf("logfile_test: all %d checks passed\n", checks);
    fu::removeTree(scratch);
    return 0;
}
