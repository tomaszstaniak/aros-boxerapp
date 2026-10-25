// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "exclusivefile.h"

#include <dos/dos.h>
#include <proto/dos.h>

namespace boxer {

bool createExclusive(const std::string &path, const std::string &data, bool *existed) {
    if (existed) *existed = false;
    BPTR lock = Lock((CONST_STRPTR)path.c_str(), SHARED_LOCK);
    if (lock) {
        UnLock(lock);
        if (existed) *existed = true;
        return false;
    }
    const LONG why = IoErr();
    if (why != ERROR_OBJECT_NOT_FOUND) {
        // Held exclusively by a writer: the name is taken.
        if (existed && why == ERROR_OBJECT_IN_USE) *existed = true;
        return false;
    }
    BPTR fh = Open((CONST_STRPTR)path.c_str(), MODE_NEWFILE);
    if (!fh) {
        const LONG e = IoErr();
        if (existed && (e == ERROR_OBJECT_IN_USE || e == ERROR_OBJECT_EXISTS)) *existed = true;
        return false;
    }
    bool ok = true;
    size_t done = 0;
    while (ok && done < data.size()) {
        const LONG n = Write(fh, (APTR)(data.data() + done), (LONG)(data.size() - done));
        if (n <= 0) ok = false;
        else done += (size_t)n;
    }
    if (!Close(fh)) ok = false;
    // A file this call created but could not fill is not left behind.
    if (!ok) DeleteFile((CONST_STRPTR)path.c_str());
    return ok;
}

} // namespace boxer
