// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "recentgames.h"

#include "fsutil.h"

#include <algorithm>

namespace boxer {

static const char *kHeader = "# Boxer recent games, newest first\n";

static std::string pathKey(const std::string &p) { return fsutil::toLower(fsutil::trimTrailingSlash(p)); }

bool sameRecentGame(const RecentGame &a, const RecentGame &b) {
	if (!a.identifier.empty() && a.identifier == b.identifier) return true;
	return !a.path.empty() && pathKey(a.path) == pathKey(b.path);
}

bool parseRecentGames(const std::string &text, std::vector<RecentGame> &out, std::string *error) {
	out.clear();
	bool versioned = false;
	size_t pos = 0, lineNo = 0;
	auto fail = [&](const std::string &why) {
		out.clear();
		if (error) *error = "line " + std::to_string(lineNo) + ": " + why;
		return false;
	};
	while (pos < text.size()) {
		size_t end = text.find('\n', pos);
		if (end == std::string::npos) end = text.size();
		std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		++lineNo;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty() || line[0] == '#') continue;
		if (!versioned) {
			if (line != "Version=1") return fail("expected Version=1");
			versioned = true;
			continue;
		}
		const size_t tab = line.find('\t');
		if (tab == std::string::npos) return fail("no tab between identifier and path");
		RecentGame g{line.substr(0, tab), line.substr(tab + 1)};
		if (g.path.empty() || g.path.find('\t') != std::string::npos) return fail("bad path");
		if (out.size() >= kMaxRecentGames) continue;
		if (std::none_of(out.begin(), out.end(), [&](const RecentGame &o) { return sameRecentGame(o, g); }))
			out.push_back(g);
	}
	if (!versioned) {
		lineNo = 0;
		return fail("no Version line");
	}
	return true;
}

std::string serializeRecentGames(const std::vector<RecentGame> &games) {
	std::string s = kHeader;
	s += "Version=1\n";
	for (const auto &g : games) s += g.identifier + "\t" + g.path + "\n";
	return s;
}

void noteRecentGame(std::vector<RecentGame> &games, const RecentGame &game) {
	games.erase(std::remove_if(games.begin(), games.end(),
	                           [&](const RecentGame &o) { return sameRecentGame(o, game); }),
	            games.end());
	games.insert(games.begin(), game);
	if (games.size() > kMaxRecentGames) games.resize(kMaxRecentGames);
}

bool renameRecentGame(std::vector<RecentGame> &games, const std::string &oldPath, const std::string &newPath) {
	bool found = false;
	for (auto &g : games)
		if (pathKey(g.path) == pathKey(oldPath)) { g.path = newPath; found = true; }
	if (!found) return false;
	// The new path may already have had an entry of its own: one stays,
	// the one with the better (earlier) place.
	std::vector<RecentGame> kept;
	for (const auto &g : games)
		if (std::none_of(kept.begin(), kept.end(), [&](const RecentGame &o) { return sameRecentGame(o, g); }))
			kept.push_back(g);
	games.swap(kept);
	return true;
}

bool removeRecentGame(std::vector<RecentGame> &games, const std::string &path) {
	const size_t before = games.size();
	games.erase(std::remove_if(games.begin(), games.end(),
	                           [&](const RecentGame &o) { return pathKey(o.path) == pathKey(path); }),
	            games.end());
	return games.size() != before;
}

RecentRead loadRecentGames(const std::string &file, std::vector<RecentGame> &games, std::string *error,
                           bool recover) {
	games.clear();
	if (recover)
		fsutil::recoverReplace(file);
	if (!fsutil::exists(file)) {
		const std::string left = fsutil::describeLeftovers(file);
		if (left.empty()) return RecentRead::Missing;
		if (error) *error = left;
		return RecentRead::Unreadable;
	}
	std::string text;
	if (!fsutil::readFile(file, text)) {
		if (error) *error = "cannot be read";
		return RecentRead::Unreadable;
	}
	return parseRecentGames(text, games, error) ? RecentRead::Ok : RecentRead::Unreadable;
}

const char *recentUpdateName(RecentUpdate u) {
	switch (u) {
	case RecentUpdate::Saved: return "saved";
	case RecentUpdate::Busy: return "busy (another Boxer is changing the list), not changed";
	case RecentUpdate::Unreadable: return "file unreadable, left as it is";
	case RecentUpdate::WriteFailed: return "NOT saved";
	}
	return "?";
}

RecentUpdate updateRecentGames(const std::string &file, const RecentGuard &guard, RecentChange change,
                               const std::function<void(std::vector<RecentGame> &)> &edit,
                               std::string *error, std::vector<RecentGame> *after) {
	if (!guard.acquire || !guard.acquire()) return RecentUpdate::Busy;
	struct Release {
		const RecentGuard &g;
		~Release() { if (g.release) g.release(); }
	} release{guard};
	std::vector<RecentGame> games;
	const RecentRead r = loadRecentGames(file, games, error);
	if (r == RecentRead::Unreadable && change == RecentChange::Add) return RecentUpdate::Unreadable;
	if (r == RecentRead::Unreadable) games.clear();
	edit(games);
	const std::string dir = fsutil::parent(file);
	if (!dir.empty() && !fsutil::isDirectory(dir)) fsutil::makeDirs(dir);
	if (!fsutil::replaceFile(file, serializeRecentGames(games), error)) return RecentUpdate::WriteFailed;
	if (after) *after = games;
	return RecentUpdate::Saved;
}

RecentRead readRecentGames(const std::string &file, const RecentGuard &guard, std::vector<RecentGame> &games,
                           std::string *error) {
	if (guard.acquire && guard.acquire()) {
		const RecentRead r = loadRecentGames(file, games, error);
		if (guard.release) guard.release();
		return r;
	}
	return loadRecentGames(file, games, error, false);
}

} // namespace boxer
