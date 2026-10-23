// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Host unit test for src/model/recentgames.{h,cpp}: the file format, the
// limit, one entry per game, renames, an unreadable file, and two writers
// interleaving.
// Usage: recentgames_test <scratch dir>
#include "../../src/model/recentgames.h"

#include "../../src/model/fsutil.h"

#include <cstdio>
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
    // The file: missing, written, read back, unreadable then replaced.
    const std::string file = fu::join(scratch, "Boxer prefs/RecentGames");
    const std::string prefs = fu::join(scratch, "Boxer prefs/Boxer.prefs");
    {
        std::vector<RecentGame> g{{"x", "y"}};
        CHECK(loadRecentGames(file, g) == RecentRead::Missing && g.empty());
        std::vector<RecentGame> after;
        CHECK(updateRecentGames(file, [](std::vector<RecentGame> &l) { noteRecentGame(l, {"a", "G:A.boxer"}); },
                                nullptr, &after));
        CHECK(after.size() == 1 && fu::isFile(file));   // its drawer was made
        CHECK(loadRecentGames(file, g) == RecentRead::Ok && g.size() == 1 && g[0].identifier == "a");
        // Unreadable: shown empty, the reason given; the settings beside it
        // are never read or written; the next write replaces the file.
        fu::writeFile(prefs, "DataDir=Work:Boxer Data\nGamesFolder=Work:Games\n");
        const std::string prefsBefore = get(prefs);
        fu::writeFile(file, "garbage\x01\x02 no version\n");
        std::string err;
        CHECK(loadRecentGames(file, g, &err) == RecentRead::Unreadable && g.empty() && !err.empty());
        CHECK(get(file) == "garbage\x01\x02 no version\n");     // reading changes nothing
        CHECK(updateRecentGames(file, [](std::vector<RecentGame> &l) {
            CHECK(l.empty());
            noteRecentGame(l, {"b", "G:B.boxer"});
        }));
        CHECK(loadRecentGames(file, g) == RecentRead::Ok && paths(g) == std::vector<std::string>{"G:B.boxer"});
        CHECK(get(prefs) == prefsBefore);
        // Clear Menu writes an empty, valid list.
        CHECK(updateRecentGames(file, [](std::vector<RecentGame> &l) { l.clear(); }));
        CHECK(loadRecentGames(file, g) == RecentRead::Ok && g.empty());
        CHECK(get(prefs) == prefsBefore);
    }
    // Two writers. Each update reads right before it writes, so writes one
    // after the other keep both entries.
    {
        updateRecentGames(file, [](std::vector<RecentGame> &l) { l.clear(); });
        updateRecentGames(file, [](std::vector<RecentGame> &l) { noteRecentGame(l, {"p1", "G:One.boxer"}); });
        updateRecentGames(file, [](std::vector<RecentGame> &l) { noteRecentGame(l, {"p2", "G:Two.boxer"}); });
        std::vector<RecentGame> g;
        CHECK(loadRecentGames(file, g) == RecentRead::Ok &&
              (paths(g) == std::vector<std::string>{"G:Two.boxer", "G:One.boxer"}));
        // Interleaved: process 2 writes between process 1's read and write.
        // Process 1's write wins and process 2's entry is lost; the file
        // stays valid and keeps what process 1 read.
        CHECK(updateRecentGames(file, [&](std::vector<RecentGame> &l) {
            CHECK(updateRecentGames(file, [](std::vector<RecentGame> &m) { noteRecentGame(m, {"p3", "G:Three.boxer"}); }));
            noteRecentGame(l, {"p4", "G:Four.boxer"});
        }));
        CHECK(loadRecentGames(file, g) == RecentRead::Ok &&
              (paths(g) == std::vector<std::string>{"G:Four.boxer", "G:Two.boxer", "G:One.boxer"}));
        // The other order: the inner (later) writer wins and the outer
        // entry is the one lost; the file is valid either way.
        CHECK(updateRecentGames(file, [&](std::vector<RecentGame> &l) {
            noteRecentGame(l, {"p5", "G:Five.boxer"});
            CHECK(updateRecentGames(file, [](std::vector<RecentGame> &m) { noteRecentGame(m, {"p6", "G:Six.boxer"}); }));
        }));
        CHECK(loadRecentGames(file, g) == RecentRead::Ok && g.size() == 4 && g[0].path == "G:Five.boxer");
        CHECK(fu::leftoversOf(file).empty());
    }
    // A write interrupted half-way: the next read finishes or rolls it back
    // and gets a valid list.
    {
        std::vector<RecentGame> before;
        loadRecentGames(file, before);
        fu::setReplaceFaultHook([](fu::ReplaceStep s) {
            return s == fu::ReplaceStep::RenameTemp ? fu::FaultAction::Crash : fu::FaultAction::Proceed;
        });
        updateRecentGames(file, [](std::vector<RecentGame> &l) { noteRecentGame(l, {"p7", "G:Seven.boxer"}); });
        fu::setReplaceFaultHook(nullptr);
        std::vector<RecentGame> g;
        CHECK(loadRecentGames(file, g) == RecentRead::Ok && paths(g) == paths(before));
        // A failed write keeps the previous file and reports it.
        fu::setReplaceFaultHook([](fu::ReplaceStep s) {
            return s == fu::ReplaceStep::WriteTemp ? fu::FaultAction::Fail : fu::FaultAction::Proceed;
        });
        std::string err;
        CHECK(!updateRecentGames(file, [](std::vector<RecentGame> &l) { l.clear(); }, &err) && !err.empty());
        fu::setReplaceFaultHook(nullptr);
        CHECK(loadRecentGames(file, g) == RecentRead::Ok && paths(g) == paths(before));
    }

    std::printf("recentgames_test: %d checks, %d failures\n", checks, failures);
    if (!failures) fu::removeTree(scratch);
    return failures ? 1 : 0;
}
