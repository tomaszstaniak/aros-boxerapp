// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Workbench arguments -> gamebox path (decision D1). Wanderer opens any
// directory as a drawer whatever its icon type (wanderer.c,
// ICONWINDOW_ACTION_OPEN dispatches on the file system entry type), so a
// double-click can reach BoxerUI only through a project icon that is not
// the gamebox drawer itself: a lone "Name.info" beside "Name.boxer"
// (sidecar) or a lone icon inside the gamebox. WBRun / OpenWorkbenchObject
// on the gamebox's own project icon and AppWindow drops hand over the
// drawer itself (tools/mkgameboxicon.py describes the icon layouts).
#pragma once

#include <dos/bptr.h>
#include <string>

struct AppMessage;

namespace boxer {

enum class GameboxArg {
    None,     // not a gamebox argument
    Drawer,   // the *.boxer drawer itself (WBRun of its icon, Shift-select, drop)
    Sidecar,  // lone project icon "Name" beside the drawer "Name.boxer"
    Inside,   // lone project icon inside the drawer (launcher icon)
    Missing,  // lone project icon whose "Name.boxer" is not beside it and
              // which is not inside a gamebox: gamebox = the expected path
};

const char *gameboxArgName(GameboxArg kind);

// Full path of a WBArg (lock + name; an empty name means the lock itself).
std::string wbArgPath(BPTR lock, const char *name);

// Classifies one WBArg; on success sets gamebox to the drawer's full path
// (for Missing: the path the sidecar expected).
GameboxArg resolveGameboxArg(BPTR lock, const char *name, std::string &gamebox);

// First gamebox among the arguments of an AppWindow/AppIcon message.
GameboxArg gameboxFromAppMessage(const struct AppMessage *msg, std::string &gamebox);

} // namespace boxer
