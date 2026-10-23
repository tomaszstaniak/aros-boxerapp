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
// An interrupted save is finished or rolled back first (fsutil::recoverReplace).
// Missing and Unreadable leave games empty.
RecentRead loadRecentGames(const std::string &file, std::vector<RecentGame> &games, std::string *error = nullptr);
// Reads the file again, applies edit, writes it with fsutil::replaceFile.
// Reading right before writing keeps what another process wrote meanwhile;
// two processes writing at the same moment can lose one entry, never the
// file. An unreadable file counts as empty and is replaced (it holds only
// history). False if the write failed; the previous file is kept then.
// *after (optional) gets the list as written.
bool updateRecentGames(const std::string &file, const std::function<void(std::vector<RecentGame> &)> &edit,
                       std::string *error = nullptr, std::vector<RecentGame> *after = nullptr);

} // namespace boxer
