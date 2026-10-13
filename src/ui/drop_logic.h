// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXWelcomeWindowController.m,
// BXImportDropzonePanelController.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// What to do with icons dropped on the Welcome or the Import window,
// without Zune or AROS headers, so it is unit-tested on the host
// (src/ui/tests/drop_logic_test.cpp). The AROS side only copies the
// paths out of the AppMessage (platform/aros/appdrop) and carries out the
// plan; a folder then goes through the same ImportController calls and
// checks as one chosen with the folder requester.
//
// The original takes several URLs and imports the first; here exactly one
// folder or one gamebox is taken, and anything else is refused with a
// sentence saying why, so a drop never starts something the user did not
// point at.
#ifndef BOXER_UI_DROP_LOGIC_H
#define BOXER_UI_DROP_LOGIC_H

#include <functional>
#include <string>
#include <vector>

namespace boxer_ui {

class ImportController;

// One dropped object as its WBArg names it: the full path (empty when it
// could not be named: the lock was gone or the path too long), and
// whether the WBArg had a name. Wanderer hands a drawer or a volume over
// as its lock alone; a file or a lone icon as its drawer's lock and name.
struct DropItem {
    std::string path;
    bool named = false;
};

enum class DropKind {
    Nothing,      // no objects in the message
    Folder,       // one drawer: a candidate import source
    Gamebox,      // one gamebox: its drawer, its sidecar icon, or an icon inside it
    Volume,       // one whole disk ("Work:")
    DiscImage,    // one disc or floppy image file
    File,         // one other file
    Missing,      // one icon whose object is not there
    Unreadable,   // the object could not be named
    Several,      // more than one object
};
const char *dropKindName(DropKind kind);

struct DropClass {
    DropKind kind = DropKind::Nothing;
    std::string path;    // Folder, Gamebox: the drawer; else the object (first of Several)
    size_t count = 0;    // objects in the message
};

// Classifies the drop by looking at the file system (fsutil).
DropClass classifyDrop(const std::vector<DropItem> &items);
// The disc and floppy image extensions recognised for a clear refusal.
bool isDiscImageName(const std::string &name);

// Where the drop landed. The Welcome window takes drops on its Import
// and DOS prompt buttons (BXWelcomeWindowController registers only those);
// the Import window takes them anywhere (the dropzone panel's whole view).
enum class DropZone { WelcomeImport, WelcomePrompt, WelcomeElsewhere, ImportWindow };
struct DropRect {
    int left = 0, top = 0, width = 0, height = 0;
    bool contains(int x, int y) const { return x >= left && y >= top && x < left + width && y < top + height; }
};
// am_MouseX/Y are relative to the window, as are the buttons' edges.
DropZone welcomeZone(int x, int y, const DropRect &importButton, const DropRect &promptButton);

enum class DropAction {
    None,       // nothing starts; `message` (if any) tells the user why
    Import,     // `path` becomes the import's source (chooseSource and its checks)
    OpenGame,   // `path`, a gamebox, opens in a new BoxerUI process
};
// Why nothing starts: each refusal has its own reason, and its message
// says it in words.
enum class DropRefusal {
    None,               // not refused (or nothing was dropped)
    OutsideZone,        // on the Welcome window, not on a button that takes drops
    ImportUnavailable,  // on the ghosted Import button (no games folder)
    PromptTakesNoFolder,// a folder on "Open a DOS prompt"
    Several, Volume, DiscImage, File, Missing, Unreadable,   // as DropKind
    CopyRunning,        // the import is copying (ImportController::Busy)
    InstallerRunning,   // the import's installer is waiting or running
    UnfinishedImport,   // the import holds its unfinished gamebox
};
const char *dropRefusalName(DropRefusal refusal);

struct DropPlan {
    DropAction action = DropAction::None;
    DropRefusal refusal = DropRefusal::None;
    std::string path;
    std::string message;
};

// importAvailable: the Import button works (a games folder is known); a
// drop on it while it is ghosted starts nothing. A folder is refused while
// the import cannot take a new source (ImportController::acceptsSource);
// a gamebox opens whatever the import is doing, since it does not touch it.
DropPlan planDrop(const DropClass &drop, DropZone zone, bool importAvailable, const ImportController &imports);

// Carries out an OpenGame plan with `launch`, which starts the gamebox in
// a new process and says whether that process was seen (with a reason when
// not). The import is not touched either way: a launch that fails only
// returns the sentence for the user.
using GameLauncher = std::function<bool(const std::string &gamebox, std::string *why)>;
struct GameStart {
    bool started = false;
    std::string message;   // for the user when not started
};
GameStart startDroppedGame(const std::string &gamebox, const GameLauncher &launch);

}  // namespace boxer_ui
#endif
