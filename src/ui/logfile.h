// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Which file a BoxerUI process logs to, without AROS headers, so it is
// unit-tested on the host (src/ui/tests/logfile_test.cpp).
//
// Every BoxerUI process has a log of its own: a game opened from its icon,
// by "Launch game" or by a drop is a second process, and two processes in
// one file overwrite each other's lines. By default the log is a new
// file in RAM:BoxerLogs/ (boxerui-<n>.log, the first free n), created so
// that it never replaces an existing file, also when two processes start
// at the same moment. A drawer of their own also keeps Wanderer's RAM Disk
// window from redrawing at every logged line.
#ifndef BOXER_UI_LOGFILE_H
#define BOXER_UI_LOGFILE_H

#include <functional>
#include <string>

namespace boxer_ui {

constexpr const char *kDefaultLogDir = "RAM:BoxerLogs";

struct LogChoice {
    std::string path;   // the file to log to; empty: no usable file
    std::string note;   // for the log: why this file and not the one asked for
};

// The first free "<stem>-<n><ext>" (n from `first`) in `dir`, created
// empty and exclusively; "" when none could be created.
std::string createNumberedLog(const std::string &dir, const std::string &stem, const std::string &ext,
                              unsigned first = 1);

// Opens `path` for writing (restarting it) and keeps it open on Opened.
// InUse: another process has it open. On AROS the open is the reservation:
// fopen "w" is Open(MODE_NEWFILE), which the handler serves with an
// exclusive (ACCESS_WRITE) lock held while the file is open, so of two
// processes opening one name at the same moment one gets it and the
// other gets ERROR_OBJECT_IN_USE. There is no check before the open, and
// so no moment between a check and the open.
enum class LogClaim { Opened, InUse, Failed };
using LogOpener = std::function<LogClaim(const std::string &path)>;

// explicitPath: the LOG argument or ToolType ("" if none). It is opened as
// given (an existing file is restarted, as before) unless another process
// has it open, as when a game icon's start reads the same LOG= ToolType
// from the program icon: then a numbered file beside it, so the running
// process keeps its log. Without LOG: a numbered file in defaultDir, which
// is created if missing; if it cannot be, its parent. Numbered files are
// created exclusively first, then opened. path is empty when nothing
// could be opened.
LogChoice chooseLogFile(const std::string &explicitPath, const std::string &defaultDir, const LogOpener &open);

}  // namespace boxer_ui
#endif
