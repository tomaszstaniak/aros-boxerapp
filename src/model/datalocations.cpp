// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXBaseAppController+BXSupportFiles.m, BXSession+BXFileManagement.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "datalocations.h"

#include "fsutil.h"
#include "gamebox.h"

namespace boxer {

std::string DataLocations::defaultDataDir(const std::string &gamesFolder)
{
	return fsutil::join(gamesFolder, "Boxer Data");
}

std::string DataLocations::gameStatesDir() const { return fsutil::join(dataDir, "Gamebox States"); }

std::string DataLocations::gameStateDir(const std::string &identifier) const
{
	return fsutil::join(gameStatesDir(), safeFolderName(identifier));
}

std::string DataLocations::currentStatePath(const std::string &identifier) const
{
	return fsutil::join(gameStateDir(identifier), "Current.boxerstate");
}

std::string DataLocations::shadowRoot(const std::string &identifier, const BundledDrive &drive) const
{
	return fsutil::join(currentStatePath(identifier), drive.shadowName);
}

std::string DataLocations::screenshotsDir() const { return fsutil::join(dataDir, "Screenshots"); }

DataDirStatus prepareDataDir(const std::string &dataDir, std::string *message, bool mayCreate)
{
	if (dataDir.empty()) {
		if (message) *message = "no data directory chosen";
		return DataDirStatus::Unset;
	}
	if (!mayCreate && !fsutil::isDirectory(dataDir)) {
		if (message) *message = dataDir + " does not exist (moved, renamed, or its volume is not mounted)";
		return DataDirStatus::Missing;
	}
	if (!fsutil::isDirectory(dataDir) && !fsutil::makeDirs(dataDir)) {
		if (message) *message = "cannot create " + dataDir;
		return DataDirStatus::CannotCreate;
	}
	if (!fsutil::isWritableDirectory(dataDir)) {
		if (message) *message = dataDir + " is not writable";
		return DataDirStatus::NotWritable;
	}
	return DataDirStatus::Ready;
}

static std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
	return s.substr(a, b - a);
}

bool parsePrefs(const std::string &text, UserPrefs &out, std::string *error)
{
	size_t pos = 0;
	int lineNo = 0;
	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos) nl = text.size();
		std::string line = trim(text.substr(pos, nl - pos));
		pos = nl + 1;
		++lineNo;
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		const size_t eq = line.find('=');
		if (eq == std::string::npos || eq == 0) {
			if (error) *error = "line " + std::to_string(lineNo) + ": expected Key=value";
			return false;
		}
		const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
		if (fsutil::toLower(key) == "datadir")
			out.dataDir = value;
		else if (fsutil::toLower(key) == "gamesfolder")
			out.gamesFolder = value;
		else
			out.other[key] = value;
	}
	return true;
}

std::string serializePrefs(const UserPrefs &prefs)
{
	std::string s = "# Boxer user settings (written by BoxerUI)\n";
	if (!prefs.dataDir.empty())
		s += "DataDir=" + prefs.dataDir + "\n";
	if (!prefs.gamesFolder.empty())
		s += "GamesFolder=" + prefs.gamesFolder + "\n";
	for (const auto &kv : prefs.other)
		s += kv.first + "=" + kv.second + "\n";
	return s;
}

static bool readPrefsFile(const std::string &path, UserPrefs &out, std::string *error)
{
	if (path.empty())
		return false;
	fsutil::recoverReplace(path);
	std::string text;
	if (!fsutil::readFile(path, text))
		return false;
	UserPrefs p;
	std::string why;
	if (!parsePrefs(text, p, &why)) {
		if (error) *error = path + ": " + why;
		return false;
	}
	out = p;
	return true;
}

PrefsSource loadUserPrefs(const DataLocations &where, UserPrefs &out, std::string *error)
{
	// Both copies are recovered, also the one not read, so no stale backup
	// or temp file waits for a later save.
	if (!where.envarcPrefsPath.empty())
		fsutil::recoverReplace(where.envarcPrefsPath);
	std::string why;
	if (readPrefsFile(where.envPrefsPath, out, &why))
		return PrefsSource::Env;
	if (readPrefsFile(where.envarcPrefsPath, out, &why))
		return PrefsSource::Envarc;
	if (why.empty())
		why = fsutil::describeLeftovers(where.envarcPrefsPath);
	if (why.empty() && !where.envPrefsPath.empty())
		why = fsutil::describeLeftovers(where.envPrefsPath);
	if (!why.empty()) {
		if (error) *error = why;
		return PrefsSource::Unreadable;
	}
	return PrefsSource::None;
}

static bool writePrefsFile(const std::string &path, const std::string &text, std::string *error)
{
	const std::string dir = fsutil::parent(path);
	if (!dir.empty() && !fsutil::isDirectory(dir) && !fsutil::makeDirs(dir)) {
		if (error) *error = "cannot create " + dir;
		return false;
	}
	return fsutil::replaceFile(path, text, error);
}

bool saveUserPrefs(const DataLocations &where, const UserPrefs &prefs, std::string *error,
                   std::string *envError)
{
	const std::string text = serializePrefs(prefs);
	if (!writePrefsFile(where.envarcPrefsPath, text, error))
		return false;
	if (!where.envPrefsPath.empty()) {
		std::string why;
		if (!writePrefsFile(where.envPrefsPath, text, &why) && envError)
			*envError = why;
	}
	return true;
}

DataDirChoice chooseDataDir(const std::string &sessionOverride, const std::string &chosen,
                            const UserPrefs &prefs, const std::string &gamesFolder)
{
	DataDirChoice c;
	if (!sessionOverride.empty()) {
		c.dataDir = sessionOverride;
		c.origin = DataDirOrigin::Override;
	} else if (!chosen.empty()) {
		c.dataDir = chosen;
		c.origin = DataDirOrigin::Chosen;
		c.save = chosen != prefs.dataDir;
	} else if (!prefs.dataDir.empty()) {
		c.dataDir = prefs.dataDir;
		c.origin = DataDirOrigin::Configured;
		c.mayCreate = false;
	} else if (!gamesFolder.empty()) {
		c.dataDir = DataLocations::defaultDataDir(gamesFolder);
		c.origin = DataDirOrigin::Proposal;
		c.save = true;
	}
	return c;
}

const char *dataDirOriginName(DataDirOrigin o)
{
	switch (o) {
	case DataDirOrigin::Override: return "session override (DATA)";
	case DataDirOrigin::Chosen: return "chosen (DATADIR/BOXER_DATADIR)";
	case DataDirOrigin::Configured: return "configured (prefs)";
	case DataDirOrigin::Proposal: return "first-run proposal";
	case DataDirOrigin::None: return "none";
	}
	return "?";
}

bool isExcludedFromScan(const std::string &path, const DataLocations &locations)
{
	if (!locations.dataDir.empty() && fsutil::isWithin(path, locations.dataDir))
		return true;
	// Any component named "Boxer Data".
	std::string p = fsutil::trimTrailingSlash(path);
	while (!p.empty()) {
		if (fsutil::toLower(fsutil::baseName(p)) == "boxer data")
			return true;
		std::string up = fsutil::parent(p);
		if (up == p || up.empty() || up.back() == ':' || up == "/")
			break;
		p = up;
	}
	return false;
}

std::string defaultGamesFolder(bool haveWork)
{
	return haveWork ? "Work:DOS Games" : "SYS:DOS Games";
}

bool acceptableGamesFolder(const std::string &path, const DataLocations &locations, std::string *why)
{
	if (path.empty()) {
		if (why) *why = "no folder chosen";
		return false;
	}
	std::string p = fsutil::trimTrailingSlash(path);
	while (!p.empty()) {
		if (fsutil::extension(fsutil::baseName(p)) == "boxer") {
			if (why) *why = path + " is inside a gamebox";
			return false;
		}
		std::string up = fsutil::parent(p);
		if (up == p || up.empty())
			break;
		p = up;
	}
	if (isExcludedFromScan(path, locations)) {
		if (why) *why = path + " is Boxer's own data folder";
		return false;
	}
	return true;
}

GamesFolderBrowse browseGamesFolder(const std::string &gamesFolder)
{
	if (gamesFolder.empty())
		return GamesFolderBrowse::NotSet;
	return fsutil::isDirectory(gamesFolder) ? GamesFolderBrowse::Open : GamesFolderBrowse::Missing;
}

static bool insideGamebox(const std::string &path)
{
	std::string p = fsutil::trimTrailingSlash(path);
	while (!p.empty()) {
		if (drivetypes::isGameboxName(fsutil::baseName(p)))
			return true;
		const std::string up = fsutil::parent(p);
		if (up == p || up.empty())
			break;
		p = up;
	}
	return false;
}

static void collectStateNames(const std::string &dir, int depth, std::vector<std::string> &out)
{
	std::vector<std::string> names;
	if (!fsutil::list(dir, names))
		return;
	for (const auto &n : names) {
		const std::string p = fsutil::join(dir, n);
		if (!fsutil::isDirectory(p))
			continue;
		if (drivetypes::isGameboxName(n)) {
			Gamebox box;
			if (box.open(p) && !box.identifier().empty())
				out.push_back(fsutil::toLower(safeFolderName(box.identifier())));
		} else if (depth > 1 && fsutil::toLower(n) != "boxer data") {
			collectStateNames(p, depth - 1, out);
		}
	}
}

std::vector<std::string> gameboxStateNamesIn(const std::string &gamesFolder)
{
	std::vector<std::string> out;
	collectStateNames(gamesFolder, 3, out);
	return out;
}

std::vector<std::string> movedDataDirCandidates(const std::string &missingDataDir,
                                                const std::string &oldGamesFolder,
                                                const std::vector<std::string> &gamesFolders)
{
	const std::string missing = fsutil::trimTrailingSlash(missingDataDir);
	std::vector<std::string> out;
	auto add = [&](const std::string &c) {
		if (c.empty() || fsutil::toLower(c) == fsutil::toLower(missing))
			return;
		for (const auto &o : out)
			if (fsutil::toLower(o) == fsutil::toLower(c))
				return;
		out.push_back(c);
	};
	const bool wasInside = !oldGamesFolder.empty() && fsutil::isWithin(missing, oldGamesFolder) &&
	                       !fsutil::relativeTo(missing, oldGamesFolder).empty();
	for (const auto &g : gamesFolders) {
		if (g.empty())
			continue;
		if (wasInside)
			add(fsutil::join(g, fsutil::relativeTo(missing, oldGamesFolder)));
		add(fsutil::join(g, fsutil::baseName(missing)));
		add(DataLocations::defaultDataDir(g));
	}
	return out;
}

MovedDataDirSearch findMovedDataDir(const std::string &missingDataDir, const std::string &oldGamesFolder,
                                    const std::vector<std::string> &gamesFolders)
{
	MovedDataDirSearch r;
	std::string uncertain;
	for (const auto &g : gamesFolders) {
		if (g.empty() || !fsutil::isDirectory(g))
			continue;
		const std::vector<std::string> games = gameboxStateNamesIn(g);
		for (const auto &c : movedDataDirCandidates(missingDataDir, oldGamesFolder, {g})) {
			if (!fsutil::isDirectory(c) || insideGamebox(c))
				continue;
			bool known = false;
			for (const auto &m : r.matching)
				known = known || fsutil::toLower(m) == fsutil::toLower(c);
			if (known)
				continue;
			std::vector<std::string> states;
			fsutil::list(fsutil::join(c, "Gamebox States"), states);
			size_t matched = 0, stateDirs = 0;
			for (const auto &s : states) {
				if (!fsutil::isDirectory(fsutil::join(fsutil::join(c, "Gamebox States"), s)))
					continue;
				++stateDirs;
				for (const auto &id : games)
					if (fsutil::toLower(s) == id) { ++matched; break; }
			}
			if (matched > 0) {
				r.matching.push_back(c);
				if (r.matching.size() == 1) r.matchedGames = matched;
			} else if ((stateDirs == 0 || games.empty()) && uncertain.empty()) {
				uncertain = c;
			}
			// State only of games that are not in this folder: some other
			// data directory, not the one that moved.
		}
	}
	if (r.matching.size() == 1) {
		r.result = MovedDataDir::Found;
		r.path = r.matching[0];
	} else if (r.matching.size() > 1) {
		r.result = MovedDataDir::Ambiguous;
		r.matchedGames = 0;
	} else if (!uncertain.empty()) {
		r.result = MovedDataDir::Uncertain;
		r.path = uncertain;
	}
	return r;
}

std::string safeFolderName(const std::string &identifier)
{
	std::string out;
	for (char c : identifier)
		out += (c == '/' || c == ':' || c == '"' || c == '*' || c == '?' || c == '#' || (unsigned char)c < 32) ? '_' : c;
	if (out.empty() || out == "." || out == "..")
		out = "_" + out;
	return out;
}

} // namespace boxer
