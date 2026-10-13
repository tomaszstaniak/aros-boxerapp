// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXWelcomeWindowController.m,
// BXImportDropzonePanelController.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "drop_logic.h"

#include "import_controller.h"

#include "../model/fsutil.h"

namespace boxer_ui {

namespace fu = boxer::fsutil;

const char *dropKindName(DropKind kind) {
    switch (kind) {
    case DropKind::Nothing: return "nothing";
    case DropKind::Folder: return "folder";
    case DropKind::Gamebox: return "gamebox";
    case DropKind::Volume: return "volume";
    case DropKind::DiscImage: return "disc image";
    case DropKind::File: return "file";
    case DropKind::Missing: return "missing";
    case DropKind::Unreadable: return "unreadable";
    case DropKind::Several: return "several";
    }
    return "?";
}

bool isDiscImageName(const std::string &name) {
    // Info.plist's image types (cue, ima, vfd, gog, inst) and the system
    // ones it relies on (iso, cdr, toast, img), plus the common PC formats.
    // No bare "bin": a game's data file is as likely as a CD image.
    static const char *const kinds[] = {"iso", "cue", "cdr", "toast", "img", "ima", "vfd",
                                        "gog", "inst", "dmg", "nrg", "mdf", "ccd"};
    const std::string ext = fu::extension(name);
    for (const char *k : kinds)
        if (ext == k) return true;
    return false;
}

static bool isGameboxDrawer(const std::string &path) {
    return fu::extension(path) == "boxer" && fu::isDirectory(path);
}

static DropClass classifyOne(const DropItem &item) {
    DropClass c;
    c.count = 1;
    c.path = fu::trimTrailingSlash(item.path);
    if (c.path.empty()) { c.kind = DropKind::Unreadable; return c; }
    // A lock on the root names the volume itself ("Work:"); a drawer never
    // ends in a colon.
    if (c.path.back() == ':') { c.kind = DropKind::Volume; return c; }
    if (isGameboxDrawer(c.path)) { c.kind = DropKind::Gamebox; return c; }
    if (fu::isDirectory(c.path)) { c.kind = DropKind::Folder; return c; }
    if (fu::exists(c.path)) {
        c.kind = isDiscImageName(c.path) ? DropKind::DiscImage : DropKind::File;
        return c;
    }
    // A lone icon: the sidecar "Name" beside "Name.boxer", or an icon inside
    // a gamebox (platform/aros/wbopen resolves Workbench arguments the
    // same way). Only a named argument can be one: a drawer or a volume
    // that is gone has nothing left to point at.
    if (item.named) {
        if (isGameboxDrawer(c.path + ".boxer")) {
            c.kind = DropKind::Gamebox;
            c.path += ".boxer";
            return c;
        }
        const std::string up = fu::parent(c.path);
        if (!up.empty() && up != c.path && isGameboxDrawer(up)) {
            c.kind = DropKind::Gamebox;
            c.path = up;
            return c;
        }
    }
    c.kind = DropKind::Missing;
    return c;
}

DropClass classifyDrop(const std::vector<DropItem> &items) {
    if (items.empty()) return {};
    if (items.size() == 1) return classifyOne(items.front());
    DropClass c;
    c.kind = DropKind::Several;
    c.count = items.size();
    c.path = items.front().path;
    return c;
}

DropZone welcomeZone(int x, int y, const DropRect &importButton, const DropRect &promptButton) {
    if (importButton.contains(x, y)) return DropZone::WelcomeImport;
    if (promptButton.contains(x, y)) return DropZone::WelcomePrompt;
    return DropZone::WelcomeElsewhere;
}

static DropPlan refuse(const std::string &message) {
    DropPlan p;
    p.message = message;
    return p;
}

// Plain quotes: the AROS system fonts are Latin-1, not UTF-8.
static std::string quoted(const std::string &path) { return "\"" + path + "\""; }

DropPlan planDrop(const DropClass &drop, DropZone zone, bool importAvailable, const ImportController &imports) {
    if (drop.kind == DropKind::Nothing) return {};
    if (zone == DropZone::WelcomeElsewhere)
        return refuse("To import a game, drop its folder on \"Import a new game\".\n"
                      "To play a gamebox, drop it on \"Import a new game\" or \"Open a DOS prompt\".");
    if (zone == DropZone::WelcomeImport && !importAvailable)
        return refuse("Boxer cannot import games until it has a games folder.");
    switch (drop.kind) {
    case DropKind::Nothing:
        return {};
    case DropKind::Several:
        return refuse(std::to_string(drop.count) + " items were dropped.\n\n"
                      "Boxer imports one game at a time: drop a single game folder or gamebox.");
    case DropKind::Volume:
        return refuse(quoted(drop.path) + " is a whole disk.\n\n"
                      "Boxer imports one game folder at a time: open the disk and drop the game's folder.");
    case DropKind::DiscImage:
        return refuse(quoted(drop.path) + " is a disc image.\n\n"
                      "Boxer cannot import disc images yet: drop the folder that holds the game's files.");
    case DropKind::File:
        return refuse(quoted(drop.path) + " is not a folder.\n\n"
                      "Boxer can only import game folders for now: drop the folder that holds the game.");
    case DropKind::Missing:
        return refuse(quoted(drop.path) + " could not be found.\n\n"
                      "Only its icon is there; the file or gamebox it belongs to may have been moved or deleted.");
    case DropKind::Unreadable:
        return refuse("Boxer could not read the name of the dropped item (its path may be too long).");
    case DropKind::Gamebox: {
        DropPlan p;
        p.action = DropAction::OpenGame;
        p.path = drop.path;
        return p;
    }
    case DropKind::Folder: {
        if (zone == DropZone::WelcomePrompt)
            return refuse("\"Open a DOS prompt\" cannot use a folder yet.\n\n"
                          "To import a game, drop its folder on \"Import a new game\".");
        std::string why;
        if (!imports.acceptsSource(&why)) return refuse(why);
        DropPlan p;
        p.action = DropAction::Import;
        p.path = drop.path;
        return p;
    }
    }
    return {};
}

}  // namespace boxer_ui
