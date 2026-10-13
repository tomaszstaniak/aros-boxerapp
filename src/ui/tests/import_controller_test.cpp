// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXImportSession.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Host unit test for src/ui/import_controller.{h,cpp}: every stage change
// and every end of an import, checked on real directories (names with
// spaces, as in tests/model) for what is left on disk: only the import's own
// unfinished gamebox ever goes; the source, other gameboxes, other files and
// icons stay byte-identical.
// Usage: import_controller_test <scratch dir>
#include "../import_controller.h"

#include "../../model/datalocations.h"
#include "../../model/fsutil.h"
#include "../../model/gamebox.h"
#include "../../model/installerscan.h"
#include "../../emulator/filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace boxer_ui;
using boxer::ImportStage;
namespace fu = boxer::fsutil;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string scratch;

static void put(const std::string &path, const std::string &data) {
    fu::makeDirs(fu::parent(path));
    if (!fu::writeFile(path, data)) { std::fprintf(stderr, "cannot write fixture %s\n", path.c_str()); std::exit(2); }
}

static std::string get(const std::string &path) {
    std::string d;
    return fu::readFile(path, d) ? d : "<missing>";
}

// Relative path -> size, mtime and contents (as tests/model/model_test.cpp).
static void snapshot(const std::string &root, const std::string &rel, std::map<std::string, std::string> &out) {
    const std::string p = rel.empty() ? root : fu::join(root, rel);
    struct stat st;
    ::stat(p.c_str(), &st);
    if (fu::isDirectory(p)) {
        out[rel + "/"] = "dir";
        std::vector<std::string> names;
        fu::list(p, names);
        for (const auto &n : names) snapshot(root, rel.empty() ? n : rel + "/" + n, out);
    } else {
        out[rel] = std::to_string((long long)st.st_size) + " " + std::to_string((long long)st.st_mtime) + " " + get(p);
    }
}
static std::map<std::string, std::string> snap(const std::string &root) {
    std::map<std::string, std::string> m;
    snapshot(root, "", m);
    return m;
}

// A file in the import's C drive, as an installer would write it. Never
// outside a gamebox: a failing check must not write into the working directory.
static void putInDrive(const ImportController &c, const std::string &rel, const std::string &data) {
    const std::string drive = c.session().rootDrivePath();
    CHECK(!drive.empty());
    if (!drive.empty()) put(fu::join(drive, rel), data);
}

static std::string dosExe() { std::string h(64, '\0'); h[0] = 'M'; h[1] = 'Z'; return h + "code"; }

// A fixture: <base>/Games is the games folder (with its Boxer Data), the
// source lies beside it, and the games folder already holds a gamebox of
// the same name with the user's save, a lone icon and helper files from an
// earlier run (.bxnew/.bxold), none of which belong to the import.
struct Fixture {
    std::string base, games, src;
    boxer::DataLocations loc;
    std::string foreignBox;
};

static Fixture makeFixture(const std::string &name, bool installer) {
    Fixture f;
    f.base = fu::join(scratch, name);
    f.games = fu::join(f.base, "Games");
    f.src = fu::join(f.base, "Tyrian 2000");
    put(fu::join(f.src, "TYRIAN.EXE"), dosExe());
    put(fu::join(f.src, "DATA/LEVEL1.DAT"), std::string(200000, 'x'));
    put(fu::join(f.src, "EMPTY.TXT"), "");
    if (installer) put(fu::join(f.src, "INSTALL.EXE"), dosExe());
    f.foreignBox = fu::join(f.games, "Tyrian 2000.boxer");
    put(fu::join(f.foreignBox, "C.harddisk/KEEP.SAV"), "the user's save");
    put(fu::join(f.foreignBox, "Game Info.plist"),
        "<plist version=\"1.0\"><dict><key>BXGameIdentifier</key><string>THEIRS</string></dict></plist>\n");
    put(fu::join(f.games, "Tyrian 2000.info"), std::string("\xe3\x10icon", 6));
    put(fu::join(f.games, "Tyrian 2000.info.bxnew"), "an earlier run's new icon");
    put(fu::join(f.games, "Tyrian 2000.info.bxold"), "an earlier run's old icon");
    put(fu::join(f.games, "Tyrian 2000 (2).info"), "a lone icon: the name is taken");
    fu::makeDirs(fu::join(f.games, "Boxer Data"));
    f.loc.dataDir = fu::join(f.games, "Boxer Data");
    return f;
}

static boxer::InstallerScanResult scan(const std::string &dir) {
    boxer::FileSystem fs;
    return boxer::scanForInstallers(fs, dir);
}

// To WaitingForInstaller or ReadyToFinalize, as ID_IMPORT_CHOOSE does.
static bool chooseAndScan(ImportController &c, const Fixture &f) {
    std::string msg;
    if (c.chooseSource(f.src, f.loc, f.games, &msg) != boxer::SourceCheck::Ok) return false;
    boxer::InstallerScanError e = boxer::InstallerScanError::None;
    return c.applyScan(scan(f.src), f.src, &e) && e == boxer::InstallerScanError::None;
}

// The new gamebox avoids "Tyrian 2000" (gamebox and icon) and
// "Tyrian 2000 (2)" (a lone icon), so it is "Tyrian 2000 (3)".
static std::string expectedBox(const Fixture &f) { return fu::join(f.games, "Tyrian 2000 (3).boxer"); }

static void copyToEnd(ImportController &c) {
    int guard = 0;
    while (c.copying() && guard++ < 1000) c.copyStep(65536);
}

static void testCopyImportToFinished() {
    Fixture f = makeFixture("Copy Finished", false);
    std::vector<std::string> log;
    std::string decoratedPath, decoratedSource;
    ImportController c([&](const std::string &l) { log.push_back(l); },
                       [&](boxer::Gamebox &box, const std::string &source, std::string &err) {
                           decoratedPath = box.path();
                           decoratedSource = source;
                           err = "no icon.library on the host";
                           return false;
                       });
    CHECK(c.open() == ImportController::Open::Started);
    CHECK(c.stage() == ImportStage::WaitingForSource);

    // A refused source keeps the import waiting and creates nothing.
    std::string msg;
    CHECK(c.chooseSource(f.games, f.loc, f.games, &msg) == boxer::SourceCheck::GamesFolder);
    CHECK(c.stage() == ImportStage::WaitingForSource && !msg.empty());

    CHECK(chooseAndScan(c, f));
    CHECK(c.stage() == ImportStage::ReadyToFinalize);
    CHECK(!c.holdsUnfinishedGamebox());

    ImportOutcome o = c.startCopy(f.games);
    CHECK(!o.ended() && !o.refused());
    CHECK(c.stage() == ImportStage::ImportingSourceFiles && c.copying());
    CHECK(c.session().createdGamebox == expectedBox(f));
    CHECK(c.holdsUnfinishedGamebox());
    CHECK(c.fileCount() == 3);
    CHECK(c.startCopy(f.games).refusal == ImportRefusal::NotNow);   // one copy per import
    long last = -1;
    bool monotonic = true;
    int guard = 0;
    while (c.copying() && guard++ < 1000) {
        o = c.copyStep(65536);
        CHECK(!o.ended());
        if (c.copying()) { monotonic = monotonic && c.copyPermille() >= last; last = c.copyPermille(); }
    }
    CHECK(monotonic);
    CHECK(c.stage() == ImportStage::ChoosingStartupProgram && !c.copying());
    const std::string dest = fu::join(expectedBox(f), "C.harddisk/tyrian20");
    CHECK(get(fu::join(dest, "DATA/LEVEL1.DAT")).size() == 200000);
    CHECK(fu::isFile(fu::join(dest, "EMPTY.TXT")));

    // The startup program cannot be saved (read-only gamebox): refused, the
    // choice stays open and nothing else changed.
    if (::chmod(expectedBox(f).c_str(), 0555) == 0 && !fu::isWritableDirectory(expectedBox(f))) {
        o = c.useStartupProgram("C.harddisk/tyrian20/TYRIAN.EXE");
        CHECK(o.refusal == ImportRefusal::Launcher && !o.error.empty());
        CHECK(c.stage() == ImportStage::ChoosingStartupProgram && decoratedPath.empty());
    } else {
        std::printf("note: read-only gamebox check skipped (running with full access)\n");
    }
    ::chmod(expectedBox(f).c_str(), 0755);

    // The startup program is saved; an icon failure is a warning, the
    // import is finished all the same.
    CHECK(c.useStartupProgram("C.harddisk/tyrian20/TYRIAN.EXE").warning == "no icon.library on the host");
    CHECK(c.stage() == ImportStage::Finished && !c.holdsUnfinishedGamebox());
    CHECK(decoratedPath == expectedBox(f) && decoratedSource == f.src);
    boxer::Gamebox box;
    CHECK(box.open(expectedBox(f)));
    auto ls = box.launchers();
    CHECK(ls.size() == 1 && ls[0].isDefault && ls[0].path == "C.harddisk/tyrian20/TYRIAN.EXE");

    // A finished import is not undone by closing, Stop or the end.
    CHECK(!c.windowClosed().ended() && c.stage() == ImportStage::Finished);
    CHECK(c.stop().refused());
    CHECK(!c.back().ended());
    CHECK(c.finishAtExit().end == ImportEnd::None);
    CHECK(fu::isDirectory(expectedBox(f)));

    // A rename by the UI moves the record with it.
    c.gameboxRenamed(fu::join(f.games, "Tyrian.boxer"));
    CHECK(c.gameName() == "Tyrian");
    // A new import after a finished one starts fresh.
    CHECK(c.open() == ImportController::Open::Started && c.stage() == ImportStage::WaitingForSource);
    CHECK(c.session().createdGamebox.empty());

    CHECK(get(fu::join(f.foreignBox, "C.harddisk/KEEP.SAV")) == "the user's save");
    CHECK(get(fu::join(f.games, "Tyrian 2000.info.bxold")) == "an earlier run's old icon");
}

static void testBackAndWindowClose() {
    Fixture f = makeFixture("Back and Close", true);
    const auto before = snap(f.base);
    ImportController c;
    CHECK(chooseAndScan(c, f));
    CHECK(c.stage() == ImportStage::WaitingForInstaller);
    ImportOutcome o = c.back();
    CHECK(o.end == ImportEnd::Cancelled && o.sourcePath == f.src && !o.hadGamebox && o.removed);
    CHECK(c.stage() == ImportStage::WaitingForSource && c.session().sourcePath.empty());

    CHECK(chooseAndScan(c, f));
    c.skipInstaller();
    CHECK(c.stage() == ImportStage::ReadyToFinalize);
    o = c.windowClosed();
    CHECK(o.end == ImportEnd::WindowClosed && !o.hadGamebox);
    CHECK(c.stage() == ImportStage::WaitingForSource);
    CHECK(c.stop().refused());               // nothing to stop
    CHECK(snap(f.base) == before);           // nothing was created anywhere
}

static void testStopWhileCopying() {
    Fixture f = makeFixture("Stop Copying", false);
    const auto before = snap(f.base);
    ImportController c;
    CHECK(chooseAndScan(c, f));
    CHECK(!c.startCopy(f.games).refused());
    CHECK(!c.copyStep(65536).ended());
    CHECK(c.copying() && fu::isDirectory(expectedBox(f)));
    // The UI asks before either of these; without the answer nothing goes.
    CHECK(c.windowClosed().refused());
    CHECK(c.back().refused());
    CHECK(fu::isDirectory(expectedBox(f)));
    ImportOutcome o = c.stop();
    CHECK(o.end == ImportEnd::Stopped && o.hadGamebox && o.removed && o.cleanupError.empty());
    CHECK(o.gameName == "Tyrian 2000 (3)" && o.sourcePath == f.src);
    CHECK(!c.copying() && c.stage() == ImportStage::WaitingForSource && c.session().createdGamebox.empty());
    CHECK(snap(f.base) == before);
    // A closed window after the stop is an ordinary close.
    CHECK(c.windowClosed().end == ImportEnd::WindowClosed);
}

static void testCopyFailure() {
    Fixture f = makeFixture("Copy Fails", false);
    const auto before = snap(f.base);
    ImportController c;
    c.copyWriteFault = [](const std::string &path, uint64_t written) {
        return !(path.find("LEVEL1.DAT") != std::string::npos && written >= 65536);
    };
    CHECK(chooseAndScan(c, f));
    CHECK(!c.startCopy(f.games).refused());
    ImportOutcome o;
    int guard = 0;
    while (c.copying() && guard++ < 1000) o = c.copyStep(65536);
    CHECK(o.end == ImportEnd::CopyFailed);
    CHECK(o.error.find("LEVEL1.DAT") != std::string::npos && o.error.find("disk full") != std::string::npos);
    CHECK(o.hadGamebox && o.removed);
    CHECK(c.stage() == ImportStage::WaitingForSource);
    CHECK(snap(f.base) == before);

    // The copy cannot start: the source went after the scan.
    Fixture g = makeFixture("Copy Cannot Start", false);
    ImportController d;
    CHECK(chooseAndScan(d, g));
    const auto gamesBefore = snap(g.games);
    fu::removeTree(g.src);
    o = d.startCopy(g.games);
    CHECK(o.end == ImportEnd::CopyFailed && !o.error.empty() && o.hadGamebox && o.removed);
    CHECK(snap(g.games) == gamesBefore);

    // The gamebox cannot be created: refused, the import stays ready.
    Fixture h = makeFixture("No Games Folder", false);
    ImportController e;
    CHECK(chooseAndScan(e, h));
    o = e.startCopy(fu::join(h.base, "No Such Folder"));
    CHECK(o.refusal == ImportRefusal::Gamebox && !o.error.empty() && !o.ended());
    CHECK(e.stage() == ImportStage::ReadyToFinalize && !fu::exists(fu::join(h.base, "No Such Folder")));
}

static void testInstallerPaths() {
    Fixture f = makeFixture("Installer", true);
    const auto before = snap(f.base);
    ImportController c;
    CHECK(chooseAndScan(c, f));
    CHECK(c.stage() == ImportStage::WaitingForInstaller);
    CHECK(c.launchInstaller(5, f.games).refusal == ImportRefusal::NotNow && !fu::exists(expectedBox(f)));
    CHECK(!c.launchInstaller(0, f.games).refused());
    CHECK(c.installing() && c.installerPending() && c.session().installerPath == "INSTALL.EXE");
    CHECK(c.session().createdGamebox == expectedBox(f) && !c.session().createdIdentifier.empty());
    // While the installer's session uses the gamebox nothing removes it.
    CHECK(c.open() == ImportController::Open::Busy);
    CHECK(c.stop().refused() && c.windowClosed().refused() && c.back().refused());
    CHECK(fu::isDirectory(expectedBox(f)));
    // The installer wrote files: on to the startup program.
    putInDrive(c, "TYRIAN/TYRIAN.EXE", dosExe());
    ImportOutcome o = c.installerEnded(SessionEnd::Normal);
    CHECK(!o.ended() && !o.refused() && c.stage() == ImportStage::ChoosingStartupProgram);
    CHECK(!c.installerPending());
    CHECK(c.installerEnded(SessionEnd::Normal).refused());   // only once
    // Unfinished: an exit takes it with it.
    o = c.finishAtExit();
    CHECK(o.end == ImportEnd::Exited && o.removed);
    CHECK(snap(f.base) == before);

    // "Finish Importing" with nothing installed: the source is copied into
    // the gamebox the installer step made.
    ImportController d;
    CHECK(chooseAndScan(d, f));
    CHECK(!d.launchInstaller(0, f.games).refused());
    d.endInstallerRequested(false);
    o = d.installerEnded(SessionEnd::Cancelled);
    CHECK(!o.ended() && d.stage() == ImportStage::ReadyToFinalize);
    CHECK(d.holdsUnfinishedGamebox());
    CHECK(d.open() == ImportController::Open::Resumed);   // not forgotten
    CHECK(!d.startCopy(f.games).refused());
    CHECK(d.session().createdGamebox == expectedBox(f));  // the same gamebox, no second one
    copyToEnd(d);
    CHECK(d.stage() == ImportStage::ChoosingStartupProgram);
    CHECK(!fu::exists(fu::join(f.games, "Tyrian 2000 (4).boxer")));
    CHECK(d.finishAtExit().removed);
    CHECK(snap(f.base) == before);
}

static void testInstallerStopAndFailure() {
    Fixture f = makeFixture("Installer Ends", true);
    const auto before = snap(f.base);
    {
        ImportController c;
        CHECK(chooseAndScan(c, f));
        CHECK(!c.launchInstaller(0, f.games).refused());
        putInDrive(c, "PART.DAT", "half installed");
        c.endInstallerRequested(true);
        CHECK(c.installerStopRequested());
        ImportOutcome o = c.installerEnded(SessionEnd::Cancelled);
        CHECK(o.end == ImportEnd::Stopped && o.hadGamebox && o.removed);
        CHECK(c.stage() == ImportStage::WaitingForSource && !c.installerPending());
        CHECK(snap(f.base) == before);
    }
    const SessionEnd failures_[] = {SessionEnd::CoreFailed, SessionEnd::PreparationFailed};
    for (SessionEnd e : failures_) {
        ImportController c;
        CHECK(chooseAndScan(c, f));
        CHECK(!c.launchInstaller(0, f.games).refused());
        putInDrive(c, "PART.DAT", "half installed");
        // A failed session stops the import even after "Finish Importing":
        // nothing half-installed becomes a game.
        c.endInstallerRequested(false);
        ImportOutcome o = c.installerEnded(e, "core said no");
        CHECK(o.end == ImportEnd::InstallerFailed && o.error == "core said no" && o.removed);
        CHECK(o.gameName == "Tyrian 2000 (3)");
        CHECK(snap(f.base) == before);
    }
}

// The import's gamebox was replaced by something that is not it (another
// gamebox moved onto its name): no end of the import deletes it, the
// failure is reported, a new import can start, and the exit retries
// without deleting it either.
static void testNeverDeletesWhatItDidNotMake() {
    Fixture f = makeFixture("Not Ours", true);
    std::vector<std::string> log;
    ImportController c([&](const std::string &l) { log.push_back(l); });
    CHECK(chooseAndScan(c, f));
    CHECK(!c.launchInstaller(0, f.games).refused());
    const std::string box = c.session().createdGamebox;
    fu::removeTree(box);
    put(fu::join(box, "Game Info.plist"),
        "<plist version=\"1.0\"><dict><key>BXGameIdentifier</key><string>SOMEONE ELSE</string></dict></plist>\n");
    put(fu::join(box, "C.harddisk/SAVE.DAT"), "precious");
    const auto before = snap(f.base);
    c.endInstallerRequested(true);
    ImportOutcome o = c.installerEnded(SessionEnd::Cancelled);
    CHECK(o.end == ImportEnd::Stopped && o.hadGamebox && !o.removed);
    CHECK(o.cleanupError.find("refusing") != std::string::npos);
    CHECK(snap(f.base) == before);
    // A new import is not blocked by the record of the old one.
    CHECK(c.open() == ImportController::Open::Started);
    CHECK(chooseAndScan(c, f));
    CHECK(!c.launchInstaller(0, f.games).refused());
    CHECK(c.session().createdGamebox != box);
    o = c.finishAtExit();
    CHECK(o.end == ImportEnd::Exited && !o.removed);   // the old one could not go
    CHECK(get(fu::join(box, "C.harddisk/SAVE.DAT")) == "precious");
    bool retried = false;
    for (const auto &l : log) retried = retried || (l.find(box) != std::string::npos && l.find("NOT deleted") != std::string::npos);
    CHECK(retried);
    CHECK(snap(f.base) == before);   // the second import's gamebox went too
}

// Leaving the controller's scope by a return or an exception removes the
// unfinished gamebox; a log hook that throws does not stop the cleanup.
static void testEarlyExit() {
    Fixture f = makeFixture("Early Exit", false);
    const auto before = snap(f.base);
    std::vector<std::string> log;
    {
        ImportController c([&](const std::string &l) { log.push_back(l); });
        CHECK(chooseAndScan(c, f));
        CHECK(!c.startCopy(f.games).refused());
        c.copyStep(65536);
        CHECK(fu::isDirectory(expectedBox(f)));
    }
    CHECK(snap(f.base) == before);
    CHECK(!log.empty() && log.back().find("unfinished at exit; gamebox deleted") != std::string::npos);

    bool caught = false;
    try {
        ImportController c;
        CHECK(chooseAndScan(c, f));
        CHECK(!c.startCopy(f.games).refused());
        throw std::runtime_error("early exit");
    } catch (const std::runtime_error &) {
        caught = true;
    }
    CHECK(caught);
    CHECK(snap(f.base) == before);

    {
        ImportController c([](const std::string &) { throw std::runtime_error("log is gone"); });
        CHECK(chooseAndScan(c, f));
        CHECK(!c.startCopy(f.games).refused());
    }
    CHECK(snap(f.base) == before);

    // An explicit end first, then the destructor: once only.
    log.clear();
    {
        ImportController c([&](const std::string &l) { log.push_back(l); });
        CHECK(chooseAndScan(c, f));
        CHECK(!c.startCopy(f.games).refused());
        CHECK(c.finishAtExit().end == ImportEnd::Exited);
        CHECK(c.finishAtExit().end == ImportEnd::None);
    }
    int ends = 0;
    for (const auto &l : log) ends += l.find("at exit") != std::string::npos;
    CHECK(ends == 1);
    CHECK(snap(f.base) == before);
}

// Name collisions: an existing gamebox, a plain file or drawer of the stem,
// and a lone icon each take a name; the import's gamebox and the icon it
// would write use the next free stem, and stopping removes only that.
static void testCollisions() {
    const char *cases[] = {"plain file", "drawer", "lone icon"};
    for (int k = 0; k < 3; ++k) {
        Fixture f = makeFixture(std::string("Collide ") + cases[k], false);
        if (k == 0) put(fu::join(f.games, "Tyrian 2000 (3)"), "a plain file");
        if (k == 1) put(fu::join(f.games, "Tyrian 2000 (3)/inside"), "a drawer's file");
        if (k == 2) put(fu::join(f.games, "Tyrian 2000 (3).info"), std::string("\xe3\x10icon", 6));
        const auto before = snap(f.base);
        std::string iconStem;
        ImportController c({}, [&](boxer::Gamebox &box, const std::string &, std::string &) {
            iconStem = fu::stripExtension(box.path());
            return true;
        });
        CHECK(chooseAndScan(c, f));
        CHECK(!c.startCopy(f.games).refused());
        CHECK(c.session().createdGamebox == fu::join(f.games, "Tyrian 2000 (4).boxer"));
        CHECK(c.stop().removed);
        CHECK(snap(f.base) == before);
        // Finished: the icon the hook would write is the new stem's.
        CHECK(chooseAndScan(c, f));
        CHECK(!c.startCopy(f.games).refused());
        copyToEnd(c);
        CHECK(c.useStartupProgram("C.harddisk/tyrian20/TYRIAN.EXE").warning.empty());
        CHECK(iconStem == fu::join(f.games, "Tyrian 2000 (4)"));
        CHECK(get(fu::join(f.games, "Tyrian 2000.info.bxnew")) == "an earlier run's new icon");
    }
}

// Before the gamebox exists another folder may be chosen (the requester or
// a drop): the earlier scan goes with the old source, a scan that does not
// belong to the current source is refused, and a refused folder leaves the
// choice already made as it was.
static void testReplaceSourceBeforeGamebox() {
    Fixture f = makeFixture("Replace Source", true);
    const std::string other = fu::join(f.base, "Other Game");
    put(fu::join(other, "PLAY.EXE"), dosExe());
    const auto before = snap(f.base);
    std::vector<std::string> log;
    ImportController c([&](const std::string &l) { log.push_back(l); });
    CHECK(chooseAndScan(c, f));
    CHECK(c.stage() == ImportStage::WaitingForInstaller && c.session().scan.installers.size() == 1);

    // A refused folder: the installer choice of the first source stays.
    std::string msg;
    CHECK(c.acceptsSource(&msg) && msg.empty() && c.busy() == ImportController::Busy::None);
    CHECK(c.chooseSource(f.games, f.loc, f.games, &msg) == boxer::SourceCheck::GamesFolder && !msg.empty());
    CHECK(c.stage() == ImportStage::WaitingForInstaller && c.session().sourcePath == f.src);
    CHECK(c.session().scan.installers.size() == 1);

    // Another folder: the old scan is gone before the new one arrives, so
    // the old installer cannot be launched for the new source.
    CHECK(c.chooseSource(other, f.loc, f.games, &msg) == boxer::SourceCheck::Ok);
    CHECK(c.stage() == ImportStage::LoadingSource && c.session().sourcePath == other);
    CHECK(c.session().scan.installers.empty() && c.session().scan.dosExecutables.empty());
    CHECK(c.launchInstaller(0, f.games).refused() && c.startCopy(f.games).refused());
    CHECK(!fu::exists(expectedBox(f)));

    // The old folder's scan, finished late, is stale: refused, nothing changes.
    boxer::InstallerScanError e = boxer::InstallerScanError::None;
    CHECK(!c.applyScan(scan(f.src), f.src, &e));
    CHECK(c.stage() == ImportStage::LoadingSource && c.session().scan.installers.empty());
    bool logged = false;
    for (const auto &l : log) logged = logged || l.find("stale scan") != std::string::npos;
    CHECK(logged);
    // The new folder's own scan is taken: no installer there, ready to copy.
    CHECK(c.applyScan(scan(other), other, &e) && e == boxer::InstallerScanError::None);
    CHECK(c.stage() == ImportStage::ReadyToFinalize && c.session().scan.installers.empty());
    // Applied once only.
    CHECK(!c.applyScan(scan(other), other, &e) && c.stage() == ImportStage::ReadyToFinalize);
    // From the ready page too: back to the first folder and its installer.
    CHECK(c.chooseSource(f.src, f.loc, f.games, &msg) == boxer::SourceCheck::Ok);
    CHECK(c.applyScan(scan(f.src), f.src, &e) && c.stage() == ImportStage::WaitingForInstaller);
    CHECK(c.session().sourcePath == f.src && c.session().scan.installers.size() == 1);
    CHECK(snap(f.base) == before);   // choosing creates nothing

    // A scan error of the new folder ends at the dropzone, as with the requester.
    const std::string empty = fu::join(f.base, "No Programs");
    put(fu::join(empty, "README.TXT"), "text");
    CHECK(c.chooseSource(empty, f.loc, f.games, &msg) == boxer::SourceCheck::Ok);
    CHECK(c.applyScan(scan(empty), empty, &e) && e == boxer::InstallerScanError::NoExecutables);
    CHECK(c.stage() == ImportStage::WaitingForSource && c.session().sourcePath.empty());
}

// While the import copies, runs its installer or holds its unfinished
// gamebox, no new source is taken: it is refused with a sentence naming
// the game, and the running import goes on untouched.
static void testRefuseSourceWhileBusy() {
    Fixture f = makeFixture("Busy", true);
    const std::string other = fu::join(f.base, "Other Game");
    put(fu::join(other, "PLAY.EXE"), dosExe());
    const auto otherBefore = snap(other);
    std::string msg;

    ImportController c;
    CHECK(chooseAndScan(c, f));
    c.skipInstaller();
    CHECK(!c.startCopy(f.games).refused());
    CHECK(!c.copyStep(65536).ended() && c.copying());
    const uint64_t copied = c.copiedBytes();
    CHECK(c.busy() == ImportController::Busy::Copying);
    CHECK(!c.acceptsSource(&msg) && msg.find("still copying") != std::string::npos);
    CHECK(msg.find("Tyrian 2000 (3)") != std::string::npos);
    msg.clear();
    CHECK(c.chooseSource(other, f.loc, f.games, &msg) != boxer::SourceCheck::Ok && !msg.empty());
    CHECK(c.copying() && c.session().sourcePath == f.src && c.copiedBytes() == copied);
    CHECK(c.open() == ImportController::Open::Resumed);
    copyToEnd(c);
    CHECK(c.stage() == ImportStage::ChoosingStartupProgram);
    CHECK(get(fu::join(expectedBox(f), "C.harddisk/tyrian20/DATA/LEVEL1.DAT")).size() == 200000);
    // Copied, startup program not chosen: still unfinished, still refused.
    CHECK(c.busy() == ImportController::Busy::UnfinishedGamebox);
    CHECK(!c.acceptsSource(&msg) && msg.find("has not finished") != std::string::npos);
    CHECK(msg.find("gamebox") != std::string::npos);
    CHECK(c.chooseSource(other, f.loc, f.games, &msg) != boxer::SourceCheck::Ok);
    CHECK(c.stage() == ImportStage::ChoosingStartupProgram && c.session().sourcePath == f.src);
    // Finished: a new import may start (open() first).
    CHECK(!c.useStartupProgram("C.harddisk/tyrian20/TYRIAN.EXE").refused());
    CHECK(c.acceptsSource(&msg));
    CHECK(c.chooseSource(other, f.loc, f.games, &msg) == boxer::SourceCheck::Missing);   // not without open()
    CHECK(c.open() == ImportController::Open::Started);
    CHECK(c.chooseSource(other, f.loc, f.games, &msg) == boxer::SourceCheck::Ok);
    CHECK(fu::isDirectory(expectedBox(f)));   // the finished gamebox stays

    ImportController d;
    CHECK(chooseAndScan(d, f));
    CHECK(!d.launchInstaller(0, f.games).refused());
    CHECK(d.busy() == ImportController::Busy::Installer);
    CHECK(!d.acceptsSource(&msg) && msg.find("installer of") != std::string::npos);
    CHECK(d.chooseSource(other, f.loc, f.games, &msg) != boxer::SourceCheck::Ok);
    CHECK(d.installing() && d.installerPending() && d.session().installerPath == "INSTALL.EXE");
    CHECK(d.finishAtExit().removed);
    CHECK(snap(other) == otherBefore);
}

int main(int argc, char **argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: import_controller_test <scratch dir>\n"); return 2; }
    scratch = argv[1];
    fu::removeTree(scratch);
    if (!fu::makeDirs(scratch)) { std::fprintf(stderr, "cannot create %s\n", scratch.c_str()); return 2; }
    testCopyImportToFinished();
    testBackAndWindowClose();
    testStopWhileCopying();
    testCopyFailure();
    testInstallerPaths();
    testInstallerStopAndFailure();
    testNeverDeletesWhatItDidNotMake();
    testEarlyExit();
    testCollisions();
    testReplaceSourceBeforeGamebox();
    testRefuseSourceWhileBusy();
    if (failures) {
        std::printf("import_controller_test: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    std::printf("import_controller_test: all %d checks passed\n", checks);
    fu::removeTree(scratch);
    return 0;
}
