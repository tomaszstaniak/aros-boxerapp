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

LogChoice chooseLogFile(const std::string &explicitPath, const std::string &defaultDir,
                        const std::function<bool(const std::string &)> &inUse) {
    LogChoice c;
    if (!explicitPath.empty()) {
        if (!inUse || !inUse(explicitPath)) {
            c.path = explicitPath;
            return c;
        }
        const std::string base = fu::baseName(explicitPath);
        std::string ext = fu::extension(base);
        std::string stem = base;
        if (!ext.empty()) {
            ext = base.substr(base.size() - ext.size() - 1);   // the original case, with the dot
            stem = base.substr(0, base.size() - ext.size());
        }
        c.path = createNumberedLog(fu::parent(explicitPath), stem, ext);
        c.note = "LOG \"" + explicitPath + "\" is open in another process; this one logs to \"" +
                 (c.path.empty() ? std::string("(nothing: no free name)") : c.path) + "\"";
        return c;
    }
    std::string dir = defaultDir;
    if (!fu::isDirectory(dir) && !fu::makeDirs(dir)) {
        c.note = "log drawer \"" + defaultDir + "\" could not be created; logging beside it";
        dir = fu::parent(defaultDir);
    }
    c.path = createNumberedLog(dir, "boxerui", ".log");
    return c;
}

}  // namespace boxer_ui
