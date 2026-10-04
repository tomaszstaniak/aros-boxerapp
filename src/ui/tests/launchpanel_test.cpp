// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (expected values from BXLaunchPanelController.m,
// BXSession.m and BXFileTypes.m), https://github.com/alunbestor/Boxer at
// commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Host unit test for src/ui/launchpanel_logic.cpp and src/model/programs.cpp.
// Expected values are worked out from the original code paths named in the
// headers. argv[1]: a scratch directory (created, may contain spaces).
#include "../launchpanel_logic.h"
#include "../../model/programs.h"
#include "../../model/datalocations.h"
#include "../../model/fsutil.h"
#include "../../model/shadowfs.h"

#include <cstdio>
#include <string>

using namespace boxer_ui;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string mz(bool extended, unsigned newHeader, const char *marker, size_t total) {
    std::string b(total, '\0');
    b[0] = 'M'; b[1] = 'Z';
    if (extended) { b[24] = 0x40; b[25] = 0; }
    b[60] = (char)(newHeader & 0xff); b[61] = (char)((newHeader >> 8) & 0xff);
    if (marker && newHeader + 2 <= total) { b[newHeader] = marker[0]; b[newHeader + 1] = marker[1]; }
    return b;
}

int main(int argc, char **argv) {
    using boxer::fsutil::join;
    // --- rows ---
    LPRow p = programRow("Work:G/C.harddisk/GAME/play.exe", "C:\\GAME\\PLAY.EXE", 'C', "");
    CHECK(p.title == "PLAY.EXE");
    CHECK(p.subtitle() == "C:\\GAME\\PLAY.EXE");
    LPRow pa = programRow("Work:G/C.harddisk/BOXTEST.COM", "C:\\BOXTEST.COM", 'C', "save");
    CHECK(pa.title == "BOXTEST.COM save");
    CHECK(pa.subtitle() == "C:\\BOXTEST.COM save");
    LPRow fav = favoriteRow("", "Work:G/C.harddisk/run.bat", "C:\\RUN.BAT", 'C', "", 0);
    CHECK(fav.title == "RUN.BAT");
    CHECK(driveRow('D', "CDROM", 1, "x").title == "Drive D (CDROM)");
    CHECK(driveRow('D', "CDROM", 1, "x").icon == LPIcon::CDROM);
    CHECK(sectionHeading(LPIcon::Recent).title == "Recent");

    // --- recents: max 3, inaccessible and launcher matches skipped ---
    std::vector<LPRecent> rec = {
        {"a/X.EXE", "C:\\X.EXE", "", 'C', true},
        {"a/BOXTEST.COM", "C:\\BOXTEST.COM", "save", 'C', true},     // matches launcher
        {"a/BOXTEST.COM", "C:\\BOXTEST.COM", "", 'C', true},         // other arguments: kept
        {"a/GONE.EXE", "", "", 0, false},
        {"a/Y.EXE", "C:\\Y.EXE", "", 'C', true},
        {"a/Z.EXE", "C:\\Z.EXE", "", 'C', true},
    };
    std::vector<LPLauncherRef> launchers = {{"A/boxtest.com", "save"}};
    auto rr = recentRows(rec, launchers);
    CHECK(rr.size() == 3);
    CHECK(rr.size() == 3 && rr[0].title == "X.EXE" && rr[1].title == "BOXTEST.COM" && rr[2].title == "Y.EXE");
    CHECK(rr.size() == 3 && rr[2].recent == 4);

    // --- keywords ---
    CHECK(filterKeywords("").empty());
    CHECK(filterKeywords("setup").size() == 1);
    CHECK(filterKeywords("a b a").size() == 2);
    auto sp = filterKeywords(" ");
    CHECK(sp.size() == 1 && sp[0].empty());   // one empty keyword: matches nothing

    // --- relevance: title 3, path 2, drive C bonus 1 ---
    LPRow setup = programRow("x/SETUP.EXE", "C:\\TOOLS\\SETUP.EXE", 'C', "");
    CHECK(relevance(setup, {"setup"}) == 3 + 2 + 1);
    CHECK(relevance(setup, {"tools"}) == 2 + 1);
    CHECK(relevance(setup, {"nothing"}) == 0);
    LPRow dsetup = programRow("x/SETUP.EXE", "D:\\SETUP.EXE", 'D', "");
    CHECK(relevance(dsetup, {"setup"}) == 5);
    CHECK(relevance(setup, {""}) == 0);

    // --- displayed rows ---
    std::vector<LPRow> favs = {fav};
    std::vector<LPRow> all = {driveRow('C', "BoxTest", 0, "src"), setup, p};
    auto d0 = displayedRows(favs, rr, all, {});
    // No filter: Favorites heading, favorites, Recent heading, recents, then
    // the drive list without an "All Programs" heading.
    CHECK(d0.size() == 1 + 1 + 1 + 3 + 3);
    CHECK(d0[0].kind == LPRow::SectionHeading && d0[0].title == "Favorites");
    CHECK(d0[2].kind == LPRow::SectionHeading && d0[2].title == "Recent");
    CHECK(d0[6].kind == LPRow::DriveHeading);
    auto d1 = displayedRows(favs, rr, all, filterKeywords("setup"));
    // Only "All Programs" matches: no headings at all.
    CHECK(d1.size() == 1 && d1[0].title == "SETUP.EXE");
    auto d2 = displayedRows(favs, rr, all, filterKeywords("exe"));
    // Recents X/Y and all-programs SETUP/PLAY match: both headings.
    CHECK(d2.size() >= 2 && d2[0].title == "Recent");
    bool sawAll = false;
    for (auto &r : d2) if (r.kind == LPRow::SectionHeading && r.title == "All Programs") sawAll = true;
    CHECK(sawAll);
    // Ties sorted by DOS path: PLAY before SETUP (same relevance 3+2+1).
    std::vector<LPRow> ties = rowsMatching(filterKeywords("exe"), all);
    CHECK(ties.size() == 2 && ties[0].title == "PLAY.EXE" && ties[1].title == "SETUP.EXE");
    CHECK(displayedRows(favs, rr, all, filterKeywords(" ")).empty());

    // --- session rules ---
    CHECK(!startWithLaunchPanel(false, true, true));
    CHECK(startWithLaunchPanel(true, true, false));
    CHECK(startWithLaunchPanel(true, false, true));
    CHECK(!startWithLaunchPanel(true, false, false));
    CHECK(afterReturnToShell(Completion::ShowLauncher, true, false, 2.9, false) == Completion::ShowPrompt);
    CHECK(afterReturnToShell(Completion::ShowLauncher, true, false, 3.0, false) == Completion::ShowLauncher);
    CHECK(afterReturnToShell(Completion::ShowLauncher, false, false, 10, false) == Completion::ShowPrompt);
    CHECK(afterReturnToShell(Completion::ShowLauncher, true, true, 0.1, false) == Completion::ShowLauncher);
    CHECK(afterReturnToShell(Completion::DoNothing, true, false, 10, true) == Completion::ShowLauncher);
    CHECK(afterReturnToShell(Completion::DoNothing, false, false, 10, true) == Completion::ShowPrompt);
    CHECK(afterReturnToShell(Completion::DoNothing, true, false, 10, false) == Completion::DoNothing);
    CHECK(afterReturnToShell(Completion::ShowPrompt, true, false, 10, true) == Completion::ShowPrompt);

    // --- recent list maintenance ---
    std::vector<boxer::RecentProgram> list;
    for (int i = 0; i < 12; ++i) boxer::noteRecentProgram(list, {"P" + std::to_string(i), ""});
    CHECK(list.size() == boxer::kRecentProgramsLimit);
    CHECK(list.front().path == "P11" && list.back().path == "P2");
    boxer::noteRecentProgram(list, {"p5", ""});           // case-insensitive: moves P5 up
    CHECK(list.size() == 10 && list.front().path == "p5");
    boxer::noteRecentProgram(list, {"p5", "x"});          // other arguments: separate entry
    CHECK(list.size() == 10 && list[1].path == "p5");
    boxer::removeRecentProgram(list, {"P5", ""});
    CHECK(list.size() == 9 && list[0].arguments == "x");

    // --- files: settings round trip and the executable scan ---
    if (argc < 2) { std::printf("no scratch directory given; file tests skipped\n"); }
    else {
        std::string root = argv[1];
        boxer::fsutil::removeTree(root);
        boxer::fsutil::makeDirs(root);
        boxer::DataLocations where;
        where.dataDir = join(root, "Boxer Data");
        std::string sp2 = boxer::GameSettings::pathFor(where, "ID/1");
        CHECK(sp2 == join(join(where.dataDir, "Game Settings"), "ID_1.plist"));
        boxer::GameSettings gs;
        CHECK(gs.load(sp2));
        CHECK(gs.recentPrograms().empty());
        gs.setRecentPrograms({{"C.harddisk/A.EXE", ""}, {"C.harddisk/B.COM", "x y"}});
        gs.setFlag(boxer::kShowLaunchPanelKey, true);
        CHECK(gs.save());
        boxer::GameSettings g2;
        CHECK(g2.load(sp2));
        auto back = g2.recentPrograms();
        CHECK(back.size() == 2 && back[1].arguments == "x y" && back[0].arguments.empty());
        CHECK(g2.flag(boxer::kShowLaunchPanelKey) && !g2.flag(boxer::kAlwaysShowLaunchPanelKey));
        g2.setRecentPrograms({});
        CHECK(g2.dict().get(boxer::kRecentProgramsKey) == nullptr);

        // Drive tree: source with shadow overlay.
        std::string src = join(root, "C Game.harddisk"), sh = join(root, "shadow C");
        boxer::fsutil::makeDirs(join(src, "TOOLS"));
        boxer::fsutil::makeDirs(join(src, "D Nested.harddisk"));
        boxer::fsutil::makeDirs(sh);
        boxer::fsutil::writeFile(join(src, "GAME.COM"), "x");
        boxer::fsutil::writeFile(join(src, "RUN.BAT"), "@echo x\r\n");
        boxer::fsutil::writeFile(join(src, "README.TXT"), "x");
        boxer::fsutil::writeFile(join(src, ".HIDDEN.EXE"), mz(false, 0, nullptr, 64));
        boxer::fsutil::writeFile(join(src, "Game Info.plist"), "x");
        boxer::fsutil::writeFile(join(join(src, "TOOLS"), "SETUP.EXE"), mz(false, 0, nullptr, 64));
        boxer::fsutil::writeFile(join(join(src, "TOOLS"), "WIN.EXE"), mz(true, 128, "PE", 400));
        boxer::fsutil::writeFile(join(join(src, "TOOLS"), "BIGSTUB.EXE"), mz(true, 4000, "PE", 4200));
        boxer::fsutil::writeFile(join(join(src, "TOOLS"), "SHORTPE.EXE"), mz(true, 128, "PE", 140));
        boxer::fsutil::writeFile(join(join(src, "TOOLS"), "OS2.EXE"), mz(true, 128, "LX", 400));
        boxer::fsutil::writeFile(join(join(src, "TOOLS"), "NOTMZ.EXE"), std::string(64, 'x'));
        boxer::fsutil::writeFile(join(join(src, "D Nested.harddisk"), "IN.EXE"), mz(false, 0, nullptr, 64));
        // State-only executable: present in the shadow, not in the source.
        boxer::fsutil::writeFile(join(sh, "NEW.BAT"), "x");
        boxer::ShadowFileSystem fs;
        fs.addMapping(src, sh);
        auto exes = boxer::scanExecutables(fs, src);
        std::vector<std::string> names;
        for (auto &e : exes) names.push_back(boxer::fsutil::relativeTo(e, src));
        std::string got;
        for (auto &n : names) got += n + ";";
        std::printf("scan: %s\n", got.c_str());
        CHECK(got == "GAME.COM;NEW.BAT;RUN.BAT;TOOLS/BIGSTUB.EXE;TOOLS/SETUP.EXE;TOOLS/SHORTPE.EXE;");
        // A deleted original disappears from the scan.
        CHECK(fs.remove(join(src, "RUN.BAT").c_str(), nullptr));
        exes = boxer::scanExecutables(fs, src);
        got.clear();
        for (auto &e : exes) got += boxer::fsutil::relativeTo(e, src) + ";";
        CHECK(got == "GAME.COM;NEW.BAT;TOOLS/BIGSTUB.EXE;TOOLS/SETUP.EXE;TOOLS/SHORTPE.EXE;");
        CHECK(boxer::executableType(fs, join(join(src, "TOOLS"), "WIN.EXE")) == boxer::ExecutableType::Windows);
        CHECK(boxer::executableType(fs, join(join(src, "TOOLS"), "OS2.EXE")) == boxer::ExecutableType::OS2);
        if (!failures) boxer::fsutil::removeTree(root);
    }

    if (failures) { std::printf("launchpanel_test: %d failure(s)\n", failures); return 1; }
    std::printf("launchpanel_test: all checks passed\n");
    return 0;
}
