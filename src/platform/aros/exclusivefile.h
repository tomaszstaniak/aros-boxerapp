// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Exclusive creation of a new file through DOS (model/fsutil.h
// setCreateExclusive, claimNewFile).
#pragma once

#include <functional>
#include <string>

namespace boxer {

// fsutil::CreateExclusive for AROS. Neither posixc's open(O_CREAT |
// O_EXCL) nor a Lock() check followed by Open(MODE_NEWFILE) is atomic:
// both look for the name first and create it afterwards, and MODE_NEWFILE
// truncates a file another process created in between. Instead the data
// goes to a scratch file in the same drawer named after this task
// (".bxc" + task address + counter, short enough for FFS's 30
// characters), which is then given the target name with Rename(). The
// RAM:, SFS and FAT handlers of both trees refuse a rename onto an
// existing name (ERROR_OBJECT_EXISTS), and each handler checks and renames
// while serving that one packet, so whoever renames first owns the name.
bool createExclusive(const std::string &path, const std::string &data, bool *existed);

// Test aid: runs between the complete scratch file and the Rename.
void setCreateExclusiveTestHook(std::function<void(const std::string &path)> hook);

} // namespace boxer
