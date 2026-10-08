// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Host path and file helpers shared by the model classes.
//
// Paths are host paths as the C library sees them: POSIX on the build host,
// "Volume:dir/file" on AROS. A path component separator is '/', and a ':'
// ends a volume or assign name; neither is ever added twice. Nothing here
// includes AROS headers.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace boxer {
namespace fsutil {

std::string join(const std::string &base, const std::string &name);
// Parent of a path; "Work:a" -> "Work:", "/a/b" -> "/a", "a" -> "".
std::string parent(const std::string &path);
std::string baseName(const std::string &path);
// Without trailing '/' (but "/" and "Vol:" are kept).
std::string trimTrailingSlash(const std::string &path);
// Extension of a file name, lower-cased, without the dot; "" when none.
// A leading dot does not start an extension (".profile").
std::string extension(const std::string &name);
std::string stripExtension(const std::string &name);
std::string toLower(std::string s);
// True if path is root itself or lies below it. ASCII case-insensitive,
// as AROS and the default macOS filesystems are.
bool isWithin(const std::string &path, const std::string &root);
// Path of `path` relative to `root` ("" for root itself); only valid when
// isWithin(path, root).
std::string relativeTo(const std::string &path, const std::string &root);

bool exists(const std::string &path);
bool isDirectory(const std::string &path);
bool isFile(const std::string &path);
bool makeDirs(const std::string &path);
// Creates exactly this directory, never reusing one: false when it already
// exists (*existed set) or cannot be created. The parent must exist.
bool makeNewDir(const std::string &path, bool *existed = nullptr);
// Names in a directory without "." and ".."; false if it cannot be read.
bool list(const std::string &dir, std::vector<std::string> &names);
bool readFile(const std::string &path, std::string &data);
bool writeFile(const std::string &path, const std::string &data);
bool copyFile(const std::string &from, const std::string &to);
bool copyTree(const std::string &from, const std::string &to);
bool removeTree(const std::string &path);
// Creates and removes a probe file inside dir.
bool isWritableDirectory(const std::string &dir);

// Replacement of a small state/prefs file without ever leaving no valid
// version behind. rename() onto an existing file is not assumed to work
// (AROS DOS Rename fails if the target exists), so the sequence is:
//   write <path>.bxnew; rename <path> -> <path>.bxold; rename .bxnew -> <path>;
//   remove .bxold.
// On a failed step the previous version is put back. After a crash,
// recoverReplace() restores <path> from .bxold and drops a stale .bxnew; call
// it before reading such a file.
//
// Only these two names, and the numbered alternatives "<path>.bxold-2" ..
// "-9" used while an earlier one cannot be removed, are ever deleted: they
// are this code's own. A "<path>.bak" or "<path>.tmp" belongs to someone
// else (a user's own copy of an icon, another program) and is never touched.
//
// The old version can outlive a successful replace: a file manager that is
// still reading it (Wanderer after a change notification) makes the delete
// fail. The new version is in place by then, so the replace succeeds, and the
// backup is remembered and removed by the next replaceFile() or by
// retryPendingCleanup(), which a program calls before it exits.
enum class ReplaceStep { WriteTemp, BackupOld, RenameTemp, RemoveBackup };
enum class FaultAction { Proceed, Fail, Crash };
// Test hook: consulted before each step. Crash returns immediately and leaves
// the files exactly as they are at that point.
using ReplaceFaultHook = std::function<FaultAction(ReplaceStep)>;
void setReplaceFaultHook(ReplaceFaultHook hook);

bool replaceFile(const std::string &path, const std::string &data, std::string *error = nullptr);
// Returns false only if a needed restore failed.
bool recoverReplace(const std::string &path);
// The first-choice names; replaceFile() may use a numbered alternative.
std::string tempPathFor(const std::string &path);
std::string backupPathFor(const std::string &path);
// Every scratch name of path that replaceFile() may use, first choice first.
std::vector<std::string> ownedScratchPaths(const std::string &path);
// Old versions whose removal failed and is still to be done.
const std::vector<std::string> &pendingCleanup();
// Tries to remove them again; returns those that still could not be removed.
std::vector<std::string> retryPendingCleanup();

} // namespace fsutil
} // namespace boxer
