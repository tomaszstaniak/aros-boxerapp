// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// The games opened most recently (the original keeps them in AppKit's
// recent documents, BXAppController noteNewRecentDocument:). Stored in a
// file of its own, ENVARC:Boxer/RecentGames, never in Boxer.prefs, so
// nothing done with the history can damage the settings:
//
//   # Boxer recent games, newest first
//   Version=1
//   <BXGameIdentifier><TAB><gamebox path>
//
// The identifier may be empty (a gamebox that has none). Two entries are
// the same game when both identifiers are set and equal, or when their
// paths are equal; a game noted again moves to the top and keeps the path
// it was opened from.
//
// Only an explicit Clear Menu may replace a file that cannot be read; an
// automatic change leaves it alone, so a newer format or a damaged file is
// never lost to a game being opened. Every change runs under a guard shared
// by all processes (RecentGuard).
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace boxer {

struct RecentGame {
	std::string identifier;
	std::string path;
};

constexpr size_t kMaxRecentGames = 10;

bool sameRecentGame(const RecentGame &a, const RecentGame &b);

// False (with the reason in error) on a missing or unknown Version line or
// a line that is not "identifier<TAB>path"; out is then empty. Entries over
// kMaxRecentGames and repeated games are dropped.
bool parseRecentGames(const std::string &text, std::vector<RecentGame> &out, std::string *error = nullptr);
std::string serializeRecentGames(const std::vector<RecentGame> &games);

// Puts game first; any entry for the same game goes. Cuts to the limit.
void noteRecentGame(std::vector<RecentGame> &games, const RecentGame &game);
// The entry with path oldPath now has newPath (and keeps its place); true
// if there was one. Paths compare case-insensitively, as AROS names do.
bool renameRecentGame(std::vector<RecentGame> &games, const std::string &oldPath, const std::string &newPath);
bool removeRecentGame(std::vector<RecentGame> &games, const std::string &path);

enum class RecentRead { Missing, Ok, Unreadable };
// recover: finish or roll back an interrupted save of this process first
// (fsutil::recoverReplace); only while holding the RecentGuard, since it
// renames files. Missing and Unreadable leave games empty.
RecentRead loadRecentGames(const std::string &file, std::vector<RecentGame> &games, std::string *error = nullptr,
                           bool recover = true);

// Exclusion between processes for a whole read-modify-replace of the file,
// recovery included. acquire waits a bounded time and says whether it got
// the guard; release is called only after a successful acquire. The AROS
// one is a named exec semaphore (platform/aros/recentlock).
struct RecentGuard {
	std::function<bool()> acquire;
	std::function<void()> release;
};

enum class RecentChange {
	Add,     // automatic (an open, an import, a rename, Remove from List):
	         // an unreadable file is left exactly as it is
	Reset,   // the user's explicit choice (Clear Menu): replaces even an
	         // unreadable file
};
enum class RecentUpdate { Saved, Busy, Unreadable, WriteFailed };
const char *recentUpdateName(RecentUpdate u);

// Under the guard: reads the file (missing = empty), applies edit, writes it
// with fsutil::replaceFile. Busy: the guard was not free in time, nothing
// read or written. Unreadable (Add only): nothing written, error says why.
// *after (optional) gets the list as written.
RecentUpdate updateRecentGames(const std::string &file, const RecentGuard &guard, RecentChange change,
                               const std::function<void(std::vector<RecentGame> &)> &edit,
                               std::string *error = nullptr, std::vector<RecentGame> *after = nullptr);

// For showing the list: under the guard when it is free, else read as it
// is without recovery (never waits longer than the guard's acquire).
RecentRead readRecentGames(const std::string &file, const RecentGuard &guard, std::vector<RecentGame> &games,
                           std::string *error = nullptr);

} // namespace boxer
