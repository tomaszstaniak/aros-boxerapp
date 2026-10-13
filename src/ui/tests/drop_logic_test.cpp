// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXWelcomeWindowController.m,
// BXImportDropzonePanelController.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Host unit test for src/ui/drop_logic.{h,cpp}: what a dropped object is
// (on real directories, names with spaces) and what the drop leads to in
// each zone and import state. A refused drop must leave the import and
// the disk as they were.
// Usage: drop_logic_test <scratch dir>
#include "../drop_logic.h"
#include "../import_controller.h"

#include "../../model/datalocations.h"
#include "../../model/fsutil.h"
#include "../../model/installerscan.h"
#include "../../emulator/filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace boxer_ui;
namespace fu = boxer::fsutil;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string scratch;

static void put(const std::string &path, const std::string &data) {
    fu::makeDirs(fu::parent(path));
    if (!fu::writeFile(path, data)) { std::fprintf(stderr, "cannot write fixture %s\n", path.c_str()); std::exit(2); }
}

static std::string dosExe() { std::string h(64, '\0'); h[0] = 'M'; h[1] = 'Z'; return h + "code"; }

static DropItem drawer(const std::string &p) { return {p + "/", false}; }   // as Wanderer names a drawer
static DropItem named(const std::string &p) { return {p, true}; }

// <base>/Games holds a gamebox with its sidecar icon (no object "Dune"
// beside it) and a lone launcher icon inside; the game folders, an image
// and a plain file lie beside it.
struct Fixture {
    std::string base, games, folder, box, image, file;
    boxer::DataLocations loc;
};

static Fixture makeFixture() {
    Fixture f;
    f.base = fu::join(scratch, "Drop Fixture");
    f.games = fu::join(f.base, "My Games");
    f.folder = fu::join(f.base, "Commander Keen");
    put(fu::join(f.folder, "KEEN4.EXE"), dosExe());
    f.box = fu::join(f.games, "Dune 2.boxer");
    put(fu::join(f.box, "C.harddisk/DUNE2.EXE"), dosExe());
    put(fu::join(f.games, "Dune 2.info"), "sidecar icon");
    put(fu::join(f.box, "Play.info"), "launcher icon");
    f.image = fu::join(f.base, "Tyrian CD.ISO");
    put(f.image, "image");
    f.file = fu::join(f.base, "readme.txt");
    put(f.file, "text");
    fu::makeDirs(fu::join(f.games, "Boxer Data"));
    f.loc.dataDir = fu::join(f.games, "Boxer Data");
    return f;
}

static void testClassify() {
    Fixture f = makeFixture();
    DropClass c = classifyDrop({drawer(f.folder)});
    CHECK(c.kind == DropKind::Folder && c.path == f.folder && c.count == 1);
    c = classifyDrop({{f.folder, false}});
    CHECK(c.kind == DropKind::Folder && c.path == f.folder);
    // The gamebox drawer, its sidecar icon (the name has no object behind
    // it), and an icon inside it: all the same gamebox.
    c = classifyDrop({drawer(f.box)});
    CHECK(c.kind == DropKind::Gamebox && c.path == f.box);
    c = classifyDrop({named(fu::join(f.games, "Dune 2"))});
    CHECK(c.kind == DropKind::Gamebox && c.path == f.box);
    c = classifyDrop({named(fu::join(f.box, "Play"))});
    CHECK(c.kind == DropKind::Gamebox && c.path == f.box);
    // A folder inside a gamebox stays a folder; the import's own check
    // refuses it (checkImportSource: Gamebox).
    c = classifyDrop({drawer(fu::join(f.box, "C.harddisk"))});
    CHECK(c.kind == DropKind::Folder);
    // Case of the extension does not matter.
    put(fu::join(f.games, "Doom.BOXER/C.harddisk/DOOM.EXE"), dosExe());
    CHECK(classifyDrop({drawer(fu::join(f.games, "Doom.BOXER"))}).kind == DropKind::Gamebox);

    c = classifyDrop({{"Work:", false}});
    CHECK(c.kind == DropKind::Volume && c.path == "Work:");
    CHECK(classifyDrop({{"RAM Disk:", false}}).kind == DropKind::Volume);
    c = classifyDrop({named(f.image)});
    CHECK(c.kind == DropKind::DiscImage && c.path == f.image);
    c = classifyDrop({named(f.file)});
    CHECK(c.kind == DropKind::File && c.path == f.file);
    // A file that happens to share a gamebox's stem is a file, not a sidecar.
    put(fu::join(f.games, "Dune 2"), "a real file");
    CHECK(classifyDrop({named(fu::join(f.games, "Dune 2"))}).kind == DropKind::File);
    fu::removeTree(fu::join(f.games, "Dune 2"));
    // A lone icon whose gamebox is gone, and an unnamed drawer that is gone.
    c = classifyDrop({named(fu::join(f.games, "Gone"))});
    CHECK(c.kind == DropKind::Missing && c.path == fu::join(f.games, "Gone"));
    CHECK(classifyDrop({drawer(fu::join(f.base, "Gone Folder"))}).kind == DropKind::Missing);
    CHECK(classifyDrop({{"", true}}).kind == DropKind::Unreadable);
    // Several objects are never narrowed to one, not even two folders.
    c = classifyDrop({drawer(f.folder), drawer(f.box)});
    CHECK(c.kind == DropKind::Several && c.count == 2);
    CHECK(classifyDrop({drawer(f.folder), drawer(f.folder), named(f.file)}).count == 3);
    CHECK(classifyDrop({}).kind == DropKind::Nothing);

    CHECK(isDiscImageName("GAME.CUE") && isDiscImageName("disk1.ima") && isDiscImageName("x.iso"));
    CHECK(!isDiscImageName("DATA.BIN") && !isDiscImageName("KEEN4.EXE") && !isDiscImageName("iso"));
}

static void testZones() {
    const DropRect import{100, 50, 40, 40}, prompt{200, 50, 40, 40};
    CHECK(welcomeZone(100, 50, import, prompt) == DropZone::WelcomeImport);
    CHECK(welcomeZone(139, 89, import, prompt) == DropZone::WelcomeImport);
    CHECK(welcomeZone(140, 89, import, prompt) == DropZone::WelcomeElsewhere);
    CHECK(welcomeZone(220, 70, import, prompt) == DropZone::WelcomePrompt);
    CHECK(welcomeZone(5, 5, import, prompt) == DropZone::WelcomeElsewhere);
    CHECK(welcomeZone(-1, -1, DropRect{}, DropRect{}) == DropZone::WelcomeElsewhere);
}

static boxer::InstallerScanResult scan(const std::string &dir) {
    boxer::FileSystem fs;
    return boxer::scanForInstallers(fs, dir);
}

static void testPlans() {
    Fixture f = makeFixture();
    ImportController idle;
    const DropClass folder = classifyDrop({drawer(f.folder)});
    const DropClass box = classifyDrop({named(fu::join(f.games, "Dune 2"))});

    DropPlan p = planDrop(folder, DropZone::WelcomeImport, true, idle);
    CHECK(p.action == DropAction::Import && p.path == f.folder && p.message.empty());
    p = planDrop(folder, DropZone::ImportWindow, true, idle);
    CHECK(p.action == DropAction::Import && p.path == f.folder);
    // The DOS prompt button opens gameboxes only.
    p = planDrop(folder, DropZone::WelcomePrompt, true, idle);
    CHECK(p.action == DropAction::None && p.message.find("Import a new game") != std::string::npos);
    p = planDrop(box, DropZone::WelcomePrompt, true, idle);
    CHECK(p.action == DropAction::OpenGame && p.path == f.box);
    p = planDrop(box, DropZone::WelcomeImport, true, idle);
    CHECK(p.action == DropAction::OpenGame && p.path == f.box);
    p = planDrop(box, DropZone::ImportWindow, true, idle);
    CHECK(p.action == DropAction::OpenGame);

    // Outside the buttons, or on the ghosted Import button: nothing starts,
    // whatever was dropped.
    for (const DropClass &c : {folder, box}) {
        p = planDrop(c, DropZone::WelcomeElsewhere, true, idle);
        CHECK(p.action == DropAction::None && !p.message.empty());
        p = planDrop(c, DropZone::WelcomeImport, false, idle);
        CHECK(p.action == DropAction::None && p.message.find("games folder") != std::string::npos);
    }

    // Unsupported objects: a sentence each, in every zone that takes drops.
    const struct { DropClass c; const char *says; } refused[] = {
        {classifyDrop({{"Work:", false}}), "whole disk"},
        {classifyDrop({named(f.image)}), "disc image"},
        {classifyDrop({named(f.file)}), "not a folder"},
        {classifyDrop({named(fu::join(f.games, "Gone"))}), "could not be found"},
        {classifyDrop({{"", true}}), "could not read"},
        {classifyDrop({drawer(f.folder), drawer(f.box)}), "2 items"},
    };
    for (const auto &r : refused)
        for (DropZone z : {DropZone::WelcomeImport, DropZone::WelcomePrompt, DropZone::ImportWindow}) {
            p = planDrop(r.c, z, true, idle);
            CHECK(p.action == DropAction::None && p.message.find(r.says) != std::string::npos);
        }
    CHECK(planDrop(DropClass{}, DropZone::ImportWindow, true, idle).message.empty());

    // A busy import refuses a folder with its own sentence and is left as
    // it was; a gamebox still opens (in another process).
    ImportController busy;
    std::string msg;
    CHECK(busy.chooseSource(f.folder, f.loc, f.games, &msg) == boxer::SourceCheck::Ok);
    CHECK(busy.applyScan(scan(f.folder), f.folder));
    CHECK(!busy.startCopy(f.games).refused() && busy.copying());
    const std::string madeBox = busy.session().createdGamebox;
    const DropClass other = classifyDrop({drawer(f.base)});
    p = planDrop(other, DropZone::ImportWindow, true, busy);
    CHECK(p.action == DropAction::None && p.message.find("still importing") != std::string::npos);
    p = planDrop(other, DropZone::WelcomeImport, true, busy);
    CHECK(p.action == DropAction::None && p.message.find("Commander Keen") != std::string::npos);
    p = planDrop(box, DropZone::WelcomeImport, true, busy);
    CHECK(p.action == DropAction::OpenGame);
    CHECK(busy.copying() && busy.session().sourcePath == f.folder && busy.session().createdGamebox == madeBox);
    CHECK(busy.stop().removed && !fu::exists(madeBox));
    // Stopped: drops are taken again.
    CHECK(planDrop(folder, DropZone::ImportWindow, true, busy).action == DropAction::Import);
}

int main(int argc, char **argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: drop_logic_test <scratch dir>\n"); return 2; }
    scratch = argv[1];
    fu::removeTree(scratch);
    if (!fu::makeDirs(scratch)) { std::fprintf(stderr, "cannot create %s\n", scratch.c_str()); return 2; }
    testClassify();
    testZones();
    testPlans();
    if (failures) {
        std::printf("drop_logic_test: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    std::printf("drop_logic_test: all %d checks passed\n", checks);
    fu::removeTree(scratch);
    return 0;
}
