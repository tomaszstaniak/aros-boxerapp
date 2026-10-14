// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "logfile.h"

#include "../model/fsutil.h"

namespace boxer_ui {

namespace fu = boxer::fsutil;

std::string createNumberedLog(const std::string &dir, const std::string &stem, const std::string &ext,
                              unsigned first) {
    // O_EXCL creation (createNewFile): of two processes trying one name,
    // one gets it and the other goes on to the next number.
    for (unsigned n = first; n < first + 10000; ++n) {
        const std::string path = fu::join(dir, stem + "-" + std::to_string(n) + ext);
        bool existed = false;
        if (fu::createNewFile(path, "", &existed)) return path;
        if (!existed) return "";   // not a name clash: the drawer is not writable
    }
    return "";
}

// A numbered file is created exclusively before it is opened, so no
// other process can have chosen the same name.
static std::string openNumbered(const std::string &dir, const std::string &stem, const std::string &ext,
                                const LogOpener &open) {
    const std::string path = createNumberedLog(dir, stem, ext);
    return !path.empty() && open && open(path) == LogClaim::Opened ? path : std::string();
}

LogChoice chooseLogFile(const std::string &explicitPath, const std::string &defaultDir, const LogOpener &open) {
    LogChoice c;
    if (!explicitPath.empty()) {
        const LogClaim claim = open ? open(explicitPath) : LogClaim::Failed;
        if (claim == LogClaim::Opened) {
            c.path = explicitPath;
            return c;
        }
        if (claim == LogClaim::Failed) {
            c.note = "LOG \"" + explicitPath + "\" could not be opened; no log";
            return c;
        }
        const std::string base = fu::baseName(explicitPath);
        std::string ext = fu::extension(base);
        std::string stem = base;
        if (!ext.empty()) {
            ext = base.substr(base.size() - ext.size() - 1);   // the original case, with the dot
            stem = base.substr(0, base.size() - ext.size());
        }
        c.path = openNumbered(fu::parent(explicitPath), stem, ext, open);
        c.note = "LOG \"" + explicitPath + "\" is open in another process; this one logs to \"" +
                 (c.path.empty() ? std::string("(nothing: no free name)") : c.path) + "\"";
        return c;
    }
    std::string dir = defaultDir;
    if (!fu::isDirectory(dir) && !fu::makeDirs(dir)) {
        c.note = "log drawer \"" + defaultDir + "\" could not be created; logging beside it";
        dir = fu::parent(defaultDir);
    }
    c.path = openNumbered(dir, "boxerui", ".log", open);
    return c;
}

}  // namespace boxer_ui
