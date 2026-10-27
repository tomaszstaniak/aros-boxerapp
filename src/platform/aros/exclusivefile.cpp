// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "exclusivefile.h"

#include "../../model/fsutil.h"

#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <cstdio>

namespace boxer {

static std::function<void(const std::string &)> g_testHook;

void setCreateExclusiveTestHook(std::function<void(const std::string &path)> hook) {
    g_testHook = std::move(hook);
}

// The task address makes the name unique among running processes; a name
// left by a process that ended is skipped like any other existing one.
static std::string scratchName(const std::string &path, int n) {
    char tail[40];
    std::snprintf(tail, sizeof tail, ".bxc%llx-%x", (unsigned long long)(IPTR)FindTask(NULL), n);
    return fsutil::join(fsutil::parent(path), tail);
}

static bool writeScratch(const std::string &scratch, const std::string &data, bool *taken) {
    *taken = false;
    BPTR lock = Lock((CONST_STRPTR)scratch.c_str(), SHARED_LOCK);
    if (lock) {
        UnLock(lock);
        *taken = true;
        return false;
    }
    if (IoErr() != ERROR_OBJECT_NOT_FOUND) {
        *taken = true;   // held by someone: not ours to touch
        return false;
    }
    // Only this task uses this name, so the check above cannot be
    // overtaken by another process.
    BPTR fh = Open((CONST_STRPTR)scratch.c_str(), MODE_NEWFILE);
    if (!fh) return false;
    bool ok = true;
    size_t done = 0;
    while (ok && done < data.size()) {
        const LONG n = Write(fh, (APTR)(data.data() + done), (LONG)(data.size() - done));
        if (n <= 0) ok = false;
        else done += (size_t)n;
    }
    if (!Close(fh)) ok = false;
    if (!ok) DeleteFile((CONST_STRPTR)scratch.c_str());
    return ok;
}

static fsutil::ClaimRename renameNoReplace(const std::string &from, const std::string &to) {
    if (Rename((CONST_STRPTR)from.c_str(), (CONST_STRPTR)to.c_str())) return fsutil::ClaimRename::Done;
    return IoErr() == ERROR_OBJECT_EXISTS ? fsutil::ClaimRename::Exists : fsutil::ClaimRename::Failed;
}

bool createExclusive(const std::string &path, const std::string &data, bool *existed) {
    fsutil::ClaimOps ops;
    ops.scratchName = scratchName;
    ops.writeScratch = writeScratch;
    ops.renameNoReplace = renameNoReplace;
    ops.removeScratch = [](const std::string &s) { DeleteFile((CONST_STRPTR)s.c_str()); };
    ops.beforeRename = g_testHook;
    return fsutil::claimNewFile(path, data, ops, existed);
}

} // namespace boxer
