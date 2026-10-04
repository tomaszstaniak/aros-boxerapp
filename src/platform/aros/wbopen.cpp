// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "wbopen.h"

#include <proto/dos.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>

#include <cctype>

namespace boxer {

static bool endsWithBoxer(const std::string &p) {
    if (p.size() <= 6) return false;
    std::string tail = p.substr(p.size() - 6);
    for (auto &c : tail) c = (char)std::tolower((unsigned char)c);
    return tail == ".boxer";
}

static bool isDirectory(const std::string &path) {
    BPTR l = Lock((CONST_STRPTR)path.c_str(), SHARED_LOCK);
    if (!l) return false;
    bool dir = false;
    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, nullptr);
    if (fib) {
        dir = Examine(l, fib) && fib->fib_DirEntryType > 0;
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(l);
    return dir;
}

static bool exists(const std::string &path) {
    BPTR l = Lock((CONST_STRPTR)path.c_str(), SHARED_LOCK);
    if (l) UnLock(l);
    return l != 0;
}

const char *gameboxArgName(GameboxArg kind) {
    switch (kind) {
    case GameboxArg::Drawer: return "drawer";
    case GameboxArg::Sidecar: return "sidecar";
    case GameboxArg::Inside: return "inside";
    case GameboxArg::Missing: return "missing";
    default: return "none";
    }
}

std::string wbArgPath(BPTR lock, const char *name) {
    static char buf[1024];
    buf[0] = 0;
    if (!lock || !NameFromLock(lock, (STRPTR)buf, sizeof buf)) return "";
    if (name && *name && !AddPart((STRPTR)buf, (CONST_STRPTR)name, sizeof buf)) return "";
    return buf;
}

GameboxArg resolveGameboxArg(BPTR lock, const char *name, std::string &gamebox) {
    std::string path = wbArgPath(lock, name);
    if (path.empty()) return GameboxArg::None;
    if (endsWithBoxer(path) && isDirectory(path)) {
        gamebox = path;
        return GameboxArg::Drawer;
    }
    // Only lone icons (no object behind them) are launchers: a real file
    // passed as an argument stays an ordinary argument.
    if (!name || !*name || exists(path)) return GameboxArg::None;
    if (isDirectory(path + ".boxer")) {
        gamebox = path + ".boxer";
        return GameboxArg::Sidecar;
    }
    std::string parent = wbArgPath(lock, nullptr);
    if (endsWithBoxer(parent) && isDirectory(parent)) {
        gamebox = parent;
        return GameboxArg::Inside;
    }
    // A lone icon is only ever a launcher, so its gamebox was moved,
    // renamed or deleted without it: say so instead of silently opening
    // the Welcome window.
    gamebox = path + ".boxer";
    return GameboxArg::Missing;
}

GameboxArg gameboxFromAppMessage(const struct AppMessage *msg, std::string &gamebox) {
    if (!msg) return GameboxArg::None;
    for (LONG i = 0; i < msg->am_NumArgs; ++i) {
        GameboxArg k = resolveGameboxArg(msg->am_ArgList[i].wa_Lock,
                                         (const char *)msg->am_ArgList[i].wa_Name, gamebox);
        if (k != GameboxArg::None && k != GameboxArg::Missing) return k;
    }
    return GameboxArg::None;
}

} // namespace boxer
