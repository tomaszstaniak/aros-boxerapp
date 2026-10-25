// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Host unit test for src/model/recentgames.{h,cpp}: the file format, the
// limit, one entry per game, renames, an unreadable file that only Clear
// Menu may replace, and two real processes under the guard.
// Usage: recentgames_test <scratch dir>
#include "../../src/model/recentgames.h"

#include "../../src/model/fsutil.h"

#include <cstdio>
#include <fcntl.h>
#include <memory>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>

using namespace boxer;
namespace fu = boxer::fsutil;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::vector<std::string> paths(const std::vector<RecentGame> &g) {
    std::vector<std::string> p;
    for (const auto &x : g) p.push_back(x.path);
    return p;
}
static std::string get(const std::string &p) { std::string d; return fu::readFile(p, d) ? d : "<missing>"; }

// The host's counterpart of the AROS semaphore: an flock on a lock file,
// tried for a bounded time. It excludes processes, which is what the test
// needs to model; the AROS mechanism is in platform/aros/recentlock.
static RecentGuard flockGuard(const std::string &lockFile) {
    auto fd = std::make_shared<int>(-1);
    return RecentGuard{
        [fd, lockFile] {
            const int f = ::open(lockFile.c_str(), O_RDWR | O_CREAT, 0644);
            if (f < 0) return false;
            for (int i = 0; i < 20; ++i) {   // ~200 ms, then busy
                if (::flock(f, LOCK_EX | LOCK_NB) == 0) { *fd = f; return true; }
                ::usleep(10000);
            }
            ::close(f);
            return false;
        },
        [fd] {
            if (*fd >= 0) { ::flock(*fd, LOCK_UN); ::close(*fd); *fd = -1; }
        }};
}

static void sendByte(int fd) { char c = 1; (void)!::write(fd, &c, 1); }
static void waitByte(int fd) { char c; (void)!::read(fd, &c, 1); }

// Two processes. First with controlled overlap: the child holds the guard
// inside its transaction while the parent tries; the parent gets Busy and
// the file is not touched; after the child commits, the parent's retry
// keeps both entries. Then both processes add 30 entries each at full
// speed, retrying on Busy: nothing is lost, the file is valid at the end.
static void testTwoProcesses(const std::string &file, const std::string &lockFile) {
    const RecentGuard guard = flockGuard(lockFile);
    updateRecentGames(file, guard, RecentChange::Reset, [](std::vector<RecentGame> &l) { l.clear(); });
    int held[2], go[2];
    if (::pipe(held) != 0 || ::pipe(go) != 0) { CHECK(!"pipe"); return; }
    std::fflush(stdout);
    const pid_t child = ::fork();
    if (child == 0) {
        fu::forgetReplaceOwnership();
        const RecentGuard mine = flockGuard(lockFile);
        const RecentUpdate u = updateRecentGames(file, mine, RecentChange::Add, [&](std::vector<RecentGame> &l) {
            sendByte(held[1]);   // inside the transaction, guard held
            waitByte(go[0]);
            noteRecentGame(l, {"child", "G:Child.boxer"});
        });
        ::_exit(u == RecentUpdate::Saved ? 0 : 1);
    }
    waitByte(held[0]);
    const std::string before = get(file);
    CHECK(updateRecentGames(file, guard, RecentChange::Add,
                            [](std::vector<RecentGame> &l) { noteRecentGame(l, {"parent", "G:Parent.boxer"}); })
          == RecentUpdate::Busy);
    CHECK(get(file) == before);
    std::vector<RecentGame> g;
    CHECK(readRecentGames(file, guard, g) == RecentRead::Ok && g.empty());   // read without the guard
    sendByte(go[1]);
    int status = 0;
    ::waitpid(child, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(updateRecentGames(file, guard, RecentChange::Add,
                            [](std::vector<RecentGame> &l) { noteRecentGame(l, {"parent", "G:Parent.boxer"}); })
          == RecentUpdate::Saved);
    CHECK(loadRecentGames(file, g) == RecentRead::Ok &&
          (paths(g) == std::vector<std::string>{"G:Parent.boxer", "G:Child.boxer"}));
    for (int fd : {held[0], held[1], go[0], go[1]}) ::close(fd);

    // Free-running: two writers, 5 entries each (the limit is 10), every
    // change retried until saved. Without the guard one would overwrite the
    // other's read-modify-write; with it all 10 must be there.
    updateRecentGames(file, guard, RecentChange::Reset, [](std::vector<RecentGame> &l) { l.clear(); });
    std::fflush(stdout);
    pid_t kids[2];
    for (int k = 0; k < 2; ++k) {
        kids[k] = ::fork();
        if (kids[k] == 0) {
            fu::forgetReplaceOwnership();
            const RecentGuard mine = flockGuard(lockFile);
            for (int i = 0; i < 5; ++i) {
                const std::string name = "G:k" + std::to_string(k) + "-" + std::to_string(i) + ".boxer";
                for (;;) {
                    const RecentUpdate u = updateRecentGames(file, mine, RecentChange::Add,
                        [&](std::vector<RecentGame> &l) { noteRecentGame(l, {"", name}); });
                    if (u == RecentUpdate::Saved) break;
                    if (u != RecentUpdate::Busy) ::_exit(2);
                }
            }
            ::_exit(0);
        }
    }
    for (pid_t k : kids) {
        int st = 0;
        ::waitpid(k, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }
    CHECK(loadRecentGames(file, g) == RecentRead::Ok && g.size() == 10);
    CHECK(fu::leftoversOf(file).empty());
}

int main(int argc, char **argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: recentgames_test <scratch dir>\n"); return 2; }
    const std::string scratch = argv[1];
    fu::removeTree(scratch);
    if (!fu::makeDirs(scratch)) { std::fprintf(stderr, "cannot create %s\n", scratch.c_str()); return 2; }

    // Format: round trip, an empty identifier, names with spaces.
    {
        std::vector<RecentGame> g{{"id-a", "Games:Dune II.boxer"}, {"", "Work:x y/Keen.boxer"}};
        std::vector<RecentGame> back;
        std::string err;
        CHECK(parseRecentGames(serializeRecentGames(g), back, &err));
        CHECK(back.size() == 2 && back[0].identifier == "id-a" && back[0].path == "Games:Dune II.boxer");
        CHECK(back[1].identifier.empty() && back[1].path == "Work:x y/Keen.boxer");
        CHECK(serializeRecentGames(g).find("Version=1\n") != std::string::npos);
        // An empty list is a valid file.
        CHECK(parseRecentGames(serializeRecentGames({}), back) && back.empty());
        // CR LF and comments are accepted.
        CHECK(parseRecentGames("# c\r\nVersion=1\r\nid\tA:b.boxer\r\n", back) && back.size() == 1 && back[0].path == "A:b.boxer");
    }
    // Unreadable texts: no version, an unknown version, a line without tab,
    // an empty path. The list comes back empty, with a reason.
    {
        std::vector<RecentGame> back{{"x", "y"}};
        std::string err;
        CHECK(!parseRecentGames("", back, &err) && back.empty() && !err.empty());
        CHECK(!parseRecentGames("id\tA:b\n", back, &err) && back.empty());
        CHECK(!parseRecentGames("Version=2\nid\tA:b\n", back, &err) && back.empty());
        CHECK(!parseRecentGames("Version=1\nA:b.boxer\n", back, &err) && back.empty() && err.find("line 2") == 0);
        CHECK(!parseRecentGames("Version=1\nid\t\n", back, &err) && back.empty());
        CHECK(!parseRecentGames("DataDir=Work:Boxer\n", back, &err) && back.empty());
    }
    // A file written by hand: over the limit and repeated games are dropped.
    {
        std::string t = "Version=1\n";
        for (int i = 0; i < 14; ++i) t += "id" + std::to_string(i) + "\tG:" + std::to_string(i) + ".boxer\n";
        t += "id0\tG:other.boxer\n";
        std::vector<RecentGame> back;
        CHECK(parseRecentGames(t, back) && back.size() == kMaxRecentGames && back[9].path == "G:9.boxer");
        CHECK(parseRecentGames("Version=1\nid\tG:a.boxer\nid\tG:b.boxer\n\tg:A.BOXER\n", back) && back.size() == 1);
    }
    // Noting: newest first, one entry per game (by identifier, or by path
    // case-insensitively), at most ten.
    {
        std::vector<RecentGame> g;
        noteRecentGame(g, {"a", "G:A.boxer"});
        noteRecentGame(g, {"b", "G:B.boxer"});
        noteRecentGame(g, {"a", "G:A.boxer"});
        CHECK((paths(g) == std::vector<std::string>{"G:A.boxer", "G:B.boxer"}));
        // The same game from another place: one entry, the new path.
        noteRecentGame(g, {"b", "H:Moved/B.boxer"});
        CHECK((paths(g) == std::vector<std::string>{"H:Moved/B.boxer", "G:A.boxer"}));
        // No identifier: the path decides.
        noteRecentGame(g, {"", "g:a.boxer/"});
        CHECK(g.size() == 2 && g[0].path == "g:a.boxer/");
        // An identifier learnt later replaces the path-only entry.
        noteRecentGame(g, {"a", "G:A.boxer"});
        CHECK(g.size() == 2 && g[0].identifier == "a");
        for (int i = 0; i < 12; ++i) noteRecentGame(g, {"n" + std::to_string(i), "N:" + std::to_string(i)});
        CHECK(g.size() == kMaxRecentGames && g[0].path == "N:11" && g[9].path == "N:2");
    }
    // Rename: the entry follows and keeps its place; an entry that already
    // had the new path merges into it; an unknown path changes nothing.
    {
        std::vector<RecentGame> g{{"a", "G:A.boxer"}, {"b", "G:B.boxer"}, {"", "G:C.boxer"}};
        CHECK(renameRecentGame(g, "g:b.boxer", "G:Bee.boxer"));
        CHECK((paths(g) == std::vector<std::string>{"G:A.boxer", "G:Bee.boxer", "G:C.boxer"}) && g[1].identifier == "b");
        CHECK(!renameRecentGame(g, "G:none.boxer", "G:x.boxer") && g.size() == 3);
        CHECK(renameRecentGame(g, "G:A.boxer", "G:C.boxer"));
        CHECK((paths(g) == std::vector<std::string>{"G:C.boxer", "G:Bee.boxer"}));
        CHECK(removeRecentGame(g, "G:BEE.boxer") && g.size() == 1);
        CHECK(!removeRecentGame(g, "G:BEE.boxer"));
    }
    // The file: missing, written, read back.
    const std::string file = fu::join(scratch, "Boxer prefs/RecentGames");
    const std::string prefs = fu::join(scratch, "Boxer prefs/Boxer.prefs");
    const std::string lockFile = fu::join(scratch, "recent.lock");
    const RecentGuard guard = flockGuard(lockFile);
    auto add = [&](const RecentGame &g, std::string *err = nullptr) {
        return updateRecentGames(file, guard, RecentChange::Add,
                                 [&](std::vector<RecentGame> &l) { noteRecentGame(l, g); }, err);
    };
    auto clearAll = [&](std::string *err = nullptr) {
        return updateRecentGames(file, guard, RecentChange::Reset, [](std::vector<RecentGame> &l) { l.clear(); }, err);
    };
    {
        std::vector<RecentGame> g{{"x", "y"}};
        CHECK(loadRecentGames(file, g) == RecentRead::Missing && g.empty());
        std::vector<RecentGame> after;
        CHECK(updateRecentGames(file, guard, RecentChange::Add,
                                [](std::vector<RecentGame> &l) { noteRecentGame(l, {"a", "G:A.boxer"}); },
                                nullptr, &after) == RecentUpdate::Saved);
        CHECK(after.size() == 1 && fu::isFile(file));   // a missing file is created, its drawer too
        CHECK(loadRecentGames(file, g) == RecentRead::Ok && g.size() == 1 && g[0].identifier == "a");
    }
    // Unreadable: an automatic change leaves the exact bytes; only Clear
    // Menu (Reset) replaces the file. The settings beside it are never read
    // or written.
    {
        fu::writeFile(prefs, "DataDir=Work:Boxer Data\nGamesFolder=Work:Games\n");
        const std::string prefsBefore = get(prefs);
        const std::string damaged = std::string("garbage\x01\x02 no version\n", 23);
        const std::string newer = "# from a newer Boxer\nVersion=2\nid\tG:Z.boxer\tmore\n";
        for (const std::string &bad : {damaged, newer}) {
            fu::writeFile(file, bad);
            std::vector<RecentGame> g;
            std::string err;
            CHECK(loadRecentGames(file, g, &err) == RecentRead::Unreadable && g.empty() && !err.empty());
            CHECK(readRecentGames(file, guard, g, &err) == RecentRead::Unreadable && g.empty());
            err.clear();
            CHECK(add({"b", "G:B.boxer"}, &err) == RecentUpdate::Unreadable && !err.empty());
            CHECK(updateRecentGames(file, guard, RecentChange::Add,
                                    [](std::vector<RecentGame> &l) { renameRecentGame(l, "G:A.boxer", "G:A2.boxer"); })
                  == RecentUpdate::Unreadable);
            CHECK(get(file) == bad);                 // exact bytes kept
            CHECK(fu::leftoversOf(file).empty());    // and nothing written beside it
            CHECK(clearAll() == RecentUpdate::Saved);
            CHECK(loadRecentGames(file, g) == RecentRead::Ok && g.empty());
        }
        // Leftovers of someone else's interrupted save with no file: also
        // unreadable, nothing created over them.
        fu::removeTree(file);
        fu::writeFile(fu::tempPathFor(file), "Version=1\n");
        fu::forgetReplaceOwnership();
        CHECK(add({"c", "G:C.boxer"}) == RecentUpdate::Unreadable && !fu::exists(file));
        fu::removeTree(fu::tempPathFor(file));
        CHECK(add({"c", "G:C.boxer"}) == RecentUpdate::Saved);
        CHECK(get(prefs) == prefsBefore);
    }
    // In one process: changes one after the other keep every entry, and a
    // change attempted while another holds the guard is refused as Busy
    // without touching the file.
    {
        clearAll();
        add({"p1", "G:One.boxer"});
        add({"p2", "G:Two.boxer"});
        std::vector<RecentGame> g;
        CHECK(loadRecentGames(file, g) == RecentRead::Ok &&
              (paths(g) == std::vector<std::string>{"G:Two.boxer", "G:One.boxer"}));
        CHECK(updateRecentGames(file, guard, RecentChange::Add, [&](std::vector<RecentGame> &l) {
            CHECK(add({"p3", "G:Three.boxer"}) == RecentUpdate::Busy);
            noteRecentGame(l, {"p4", "G:Four.boxer"});
        }) == RecentUpdate::Saved);
        CHECK(loadRecentGames(file, g) == RecentRead::Ok &&
              (paths(g) == std::vector<std::string>{"G:Four.boxer", "G:Two.boxer", "G:One.boxer"}));
        // A guard that cannot be had: nothing read or written.
        const RecentGuard never{[] { return false; }, [] {}};
        const std::string before = get(file);
        CHECK(updateRecentGames(file, never, RecentChange::Reset, [](std::vector<RecentGame> &l) { l.clear(); })
              == RecentUpdate::Busy && get(file) == before);
        CHECK(readRecentGames(file, never, g) == RecentRead::Ok && g.size() == 3);
    }
    // Two real processes.
    testTwoProcesses(file, lockFile);
    // A write interrupted half-way: the next read finishes or rolls it back
    // and gets a valid list. A failed write keeps the previous file.
    {
        std::vector<RecentGame> before;
        loadRecentGames(file, before);
        fu::setReplaceFaultHook([](fu::ReplaceStep s) {
            return s == fu::ReplaceStep::RenameTemp ? fu::FaultAction::Crash : fu::FaultAction::Proceed;
        });
        add({"p7", "G:Seven.boxer"});
        fu::setReplaceFaultHook(nullptr);
        std::vector<RecentGame> g;
        CHECK(loadRecentGames(file, g) == RecentRead::Ok && paths(g) == paths(before));
        fu::setReplaceFaultHook([](fu::ReplaceStep s) {
            return s == fu::ReplaceStep::WriteTemp ? fu::FaultAction::Fail : fu::FaultAction::Proceed;
        });
        std::string err;
        CHECK(clearAll(&err) == RecentUpdate::WriteFailed && !err.empty());
        fu::setReplaceFaultHook(nullptr);
        CHECK(loadRecentGames(file, g) == RecentRead::Ok && paths(g) == paths(before));
    }
    // The temp name is claimed by an exclusive create: a name another writer
    // already holds is skipped, never written over.
    {
        fu::forgetReplaceOwnership();
        const std::string foreign = fu::tempPathFor(file);
        fu::writeFile(foreign, "another writer's temp");
        CHECK(add({"p8", "G:Eight.boxer"}) == RecentUpdate::Saved);
        CHECK(get(foreign) == "another writer's temp");
        std::vector<RecentGame> g;
        CHECK(loadRecentGames(file, g) == RecentRead::Ok && g[0].path == "G:Eight.boxer");
        fu::removeTree(foreign);
        bool existed = false;
        CHECK(!fu::createNewFile(file, "x", &existed) && existed && get(file) != "x");
    }

    std::printf("recentgames_test: %d checks, %d failures\n", checks, failures);
    if (!failures) fu::removeTree(scratch);
    return failures ? 1 : 0;
}
