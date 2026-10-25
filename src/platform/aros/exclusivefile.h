// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Exclusive creation of a new file through DOS (model/fsutil.h
// setCreateExclusive).
#pragma once

#include <string>

namespace boxer {

// fsutil::CreateExclusive for AROS. posixc's open(O_CREAT | O_EXCL) checks
// for the name with Lock() and then opens, and its fopen("w") reserves
// nothing at all. Here: no object of that name (Lock fails with "object
// not found"), then Open(MODE_NEWFILE), whose handle keeps the file locked
// for writing while it is filled, so a second MODE_NEWFILE of the same name
// fails with "object in use" (RAM:, SFS). The check-then-open gap that is
// left is closed for the recent games file by its guard (recentlock.h).
bool createExclusive(const std::string &path, const std::string &data, bool *existed);

} // namespace boxer
