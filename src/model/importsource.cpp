// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXImportSession.m, BXAppController+BXGamesFolder.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "importsource.h"

#include "datalocations.h"
#include "fsutil.h"
#include "gamebox.h"
#include "sourcecopy.h"

#include <cctype>
#include <regex>

namespace boxer {

static bool insideGamebox(const std::string &path)
{
	std::string p = fsutil::trimTrailingSlash(path);
	while (!p.empty()) {
		if (fsutil::extension(fsutil::baseName(p)) == "boxer")
			return true;
		std::string up = fsutil::parent(p);
		if (up == p || up.empty())
			break;
		p = up;
	}
	return false;
}

SourceCheck checkImportSource(const std::string &path, const DataLocations &locations,
                              const std::string &gamesFolder, std::string *message)
{
	// Plain quotes: the AROS system fonts are Latin-1, not UTF-8.
	const std::string name = "\"" + path + "\"";
	auto say = [&](SourceCheck c, const std::string &text) {
		if (message) *message = text;
		return c;
	};
	if (path.empty() || !fsutil::exists(path))
		return say(SourceCheck::Missing, name + " could not be found.");
	if (!fsutil::isDirectory(path))
		return say(SourceCheck::NotFolder, name + " is not a folder. Boxer can only import game folders for now.");
	if (insideGamebox(path))
		return say(SourceCheck::Gamebox, name + " is already a gamebox. Open it instead of importing it.");
	if (isExcludedFromScan(path, locations))
		return say(SourceCheck::BoxerData, name + " is where Boxer keeps its own data and cannot be imported.");
	if (!gamesFolder.empty() && fsutil::isWithin(gamesFolder, path))
		return say(SourceCheck::GamesFolder, name + " is or contains your games folder and cannot be imported.");
	if (!locations.dataDir.empty() && fsutil::isWithin(locations.dataDir, path))
		return say(SourceCheck::ContainsDataDir, name + " contains Boxer's own data and cannot be imported.");
	if (message) message->clear();
	return SourceCheck::Ok;
}

SourceCheck ImportSession::chooseSource(const std::string &path, const DataLocations &locations,
                                        const std::string &gamesFolder, std::string *message)
{
	SourceCheck c = checkImportSource(path, locations, gamesFolder, message);
	if (c == SourceCheck::Ok) {
		sourcePath = path;
		stage = ImportStage::LoadingSource;
	} else {
		sourcePath.clear();
		stage = ImportStage::WaitingForSource;
	}
	return c;
}

void ImportSession::cancelSourceSelection()
{
	sourcePath.clear();
	scan = InstallerScanResult();
	stage = ImportStage::WaitingForSource;
}

InstallerScanError ImportSession::applyScan(const InstallerScanResult &result)
{
	scan = result;
	if (result.error != InstallerScanError::None) {
		const InstallerScanError e = result.error;
		cancelSourceSelection();
		return e;
	}
	stage = result.shouldOfferInstallers() ? ImportStage::WaitingForInstaller : ImportStage::ReadyToFinalize;
	return InstallerScanError::None;
}

void ImportSession::skipInstaller()
{
	if (stage == ImportStage::WaitingForInstaller)
		stage = ImportStage::ReadyToFinalize;
}

std::string gameboxNameForSource(const std::string &original)
{
	std::string name = original;
	const std::string ext = fsutil::extension(name);
	if (ext == "boxer" || ext == "cdrom" || ext == "floppy" || ext == "harddisk")
		name = fsutil::stripExtension(name);
	name = std::regex_replace(name, std::regex("[\\[\\(]+.*[\\]\\)]+"), "");
	name = std::regex_replace(name, std::regex("([a-zA-Z]+)(\\d+)"), "$1 $2");
	name = std::regex_replace(name, std::regex("[_-]"), " ");
	name = std::regex_replace(name, std::regex("\\s+"), " ");
	while (!name.empty() && name.front() == ' ') name.erase(0, 1);
	while (!name.empty() && name.back() == ' ') name.pop_back();
	std::string out, word;
	const std::regex roman("^[Ii]?[XxVvIi][Ii]*$");
	auto flush = [&]() {
		if (word.empty()) return;
		if (std::regex_match(word, roman)) {
			for (char &c : word) c = (char)std::toupper((unsigned char)c);
		} else {
			// NSString capitalizedString: first letter of the word upper,
			// the rest lower.
			for (size_t i = 0; i < word.size(); ++i)
				word[i] = (char)(i ? std::tolower((unsigned char)word[i]) : std::toupper((unsigned char)word[i]));
		}
		if (!out.empty()) out += ' ';
		out += word;
		word.clear();
	};
	for (char c : name) {
		if (c == ' ') flush();
		else word += c;
	}
	flush();
	return out.empty() ? original : out;
}

std::string shortenMiddle(const std::string &text, size_t maxChars)
{
	if (maxChars < 5) maxChars = 5;
	if (text.size() <= maxChars)
		return text;
	const size_t keep = maxChars - 3;
	const size_t head = (keep + 1) / 2, tail = keep - head;
	std::string a = text.substr(0, head), b = text.substr(text.size() - tail);
	// Whole words when a word break is near the cut: "The Secret of ...
	// Part 2" reads better than "The Secret of Monke...on Part 2". Each side
	// only gets shorter, so the result stays within maxChars.
	const size_t near = 8;
	bool wordA = false, wordB = false;
	const size_t sa = a.rfind(' ');
	if (sa != std::string::npos && sa > 0 && a.size() - sa <= near) { a.resize(sa); wordA = true; }
	const size_t sb = b.find(' ');
	if (sb != std::string::npos && sb < near && sb + 1 < b.size()) { b.erase(0, sb + 1); wordB = true; }
	while (!a.empty() && a.back() == ' ') a.pop_back();
	while (!b.empty() && b.front() == ' ') b.erase(0, 1);
	return a + (wordA ? " ..." : "...") + (wordB ? " " : "") + b;
}

std::string startupProgramText(const std::string &name, size_t maxNameChars)
{
	return "Choose the program that starts\n\"" + shortenMiddle(name, maxNameChars) + "\":";
}

std::string readyToImportText(const std::string &name, size_t maxNameChars)
{
	return "\"" + shortenMiddle(name, maxNameChars) + "\"\nis ready to be imported into your games folder.";
}

std::string validGameboxName(const std::string &name)
{
	size_t i = 0;
	while (i < name.size() && name[i] == '.') ++i;
	std::string out = name.substr(i);
	for (char &c : out)
		if (c == '/' || c == '\\' || c == ':') c = '-';
	return out;
}

std::string incrementedGameboxName(const std::string &name, unsigned increment)
{
	return increment <= 1 ? name + ".boxer" : name + " (" + std::to_string(increment) + ").boxer";
}

bool ImportSession::createGamebox(const std::string &folder, std::string *error)
{
	if (!createdGamebox.empty()) {
		if (error) *error = "a gamebox was already created by this import";
		return false;
	}
	if (!fsutil::isDirectory(folder)) {
		if (error) *error = folder + " does not exist";
		return false;
	}
	std::string name = validGameboxName(gameboxNameForSource(fsutil::baseName(fsutil::trimTrailingSlash(sourcePath))));
	if (name.empty()) name = "Game";
	std::string path;
	for (unsigned n = 1; n < 1000; ++n) {
		const std::string candidate = fsutil::join(folder, incrementedGameboxName(name, n));
		// The import also writes "<stem>.info" beside the gamebox, and
		// Wanderer shows a lone "<stem>.info" as the icon of "<stem>": a
		// name is free only when none of the three exist, so no existing
		// file, drawer or icon is ever overwritten or adopted.
		const std::string stem = fsutil::stripExtension(candidate);
		if (fsutil::exists(stem) || fsutil::exists(stem + ".info")) continue;
		bool existed = false;
		if (fsutil::makeNewDir(candidate, &existed)) { path = candidate; break; }
		if (!existed) {
			if (error) *error = "cannot create " + candidate;
			return false;
		}
	}
	if (path.empty()) {
		if (error) *error = "no free gamebox name for " + name;
		return false;
	}
	// From here on the directory is ours: record it first, so a failure
	// below is cleaned up by discardGamebox and by nothing wider.
	createdGamebox = path;
	gamesFolder = folder;
	std::string why;
	Gamebox box;
	bool persisted = false;
	if (!fsutil::makeNewDir(rootDrivePath()) || !box.open(path, &why) ||
	    (createdIdentifier = box.ensureIdentifier(&persisted, &why)).empty() || !persisted) {
		createdIdentifier.clear();
		if (error) *error = "cannot prepare " + path + (why.empty() ? "" : ": " + why);
		discardGamebox();
		return false;
	}
	return true;
}

std::string ImportSession::rootDrivePath() const
{
	return createdGamebox.empty() ? "" : fsutil::join(createdGamebox, "C.harddisk");
}

std::string ImportSession::gameName() const
{
	const std::string base = fsutil::baseName(fsutil::trimTrailingSlash(createdGamebox));
	return fsutil::extension(base) == "boxer" ? fsutil::stripExtension(base) : base;
}

bool ImportSession::prepareCopyDestination(std::string &destination, std::string *error) const
{
	destination = rootDrivePath();
	if (destination.empty()) {
		if (error) *error = "no gamebox";
		return false;
	}
	if (!shouldUseSubfolderForSource(sourcePath))
		return true;
	std::string dos = validDOSName(fsutil::baseName(fsutil::trimTrailingSlash(sourcePath)));
	if (dos.empty()) dos = "game";
	destination = fsutil::join(destination, dos);
	if (!fsutil::isDirectory(destination) && !fsutil::makeNewDir(destination)) {
		if (error) *error = "cannot create " + destination;
		return false;
	}
	return true;
}

static bool anyFileBelow(const std::string &dir, int depth)
{
	std::vector<std::string> names;
	if (depth > 32 || !fsutil::list(dir, names))
		return false;
	for (const auto &n : names) {
		if (n.empty() || n[0] == '.')
			continue;
		const std::string p = fsutil::join(dir, n);
		if (fsutil::isFile(p) || (fsutil::isDirectory(p) && anyFileBelow(p, depth + 1)))
			return true;
	}
	return false;
}

bool ImportSession::gameDidInstall() const
{
	return !createdGamebox.empty() && anyFileBelow(rootDrivePath(), 0);
}

bool ImportSession::discardGamebox(std::string *error)
{
	if (createdGamebox.empty())
		return true;
	// Defence in depth for cancel and failure: the recorded path must still be what this
	// session made, a ".boxer" directly inside the games folder.
	const bool shapeOk = fsutil::extension(fsutil::baseName(createdGamebox)) == "boxer" &&
	                     fsutil::trimTrailingSlash(fsutil::parent(createdGamebox)) == fsutil::trimTrailingSlash(gamesFolder);
	// And it must still carry the identifier written at creation; before
	// that point it may hold nothing but the empty C.harddisk.
	bool ownOk = false;
	if (shapeOk && fsutil::exists(createdGamebox)) {
		if (!createdIdentifier.empty()) {
			Gamebox box;
			ownOk = box.open(createdGamebox) && box.identifier() == createdIdentifier;
		} else {
			std::vector<std::string> names;
			ownOk = fsutil::list(createdGamebox, names) &&
			        (names.empty() || (names.size() == 1 && names[0] == "C.harddisk" && !anyFileBelow(rootDrivePath(), 0)));
		}
	} else if (shapeOk) {
		ownOk = true; // already gone
	}
	if (!ownOk) {
		if (error) *error = "refusing to delete " + createdGamebox + ": not the gamebox this import created";
		return false;
	}
	if (fsutil::exists(createdGamebox) && !fsutil::removeTree(createdGamebox)) {
		if (error) *error = "could not delete " + createdGamebox;
		return false;
	}
	createdGamebox.clear();
	return true;
}

bool installerSessionCommands(const std::string &sourcePath, const std::string &installerRelative,
                              std::vector<std::string> &preflight, std::vector<std::string> &launch)
{
	std::string quoted;
	if (!dosQuote(fsutil::trimTrailingSlash(sourcePath), quoted))
		return false;
	preflight.push_back("MOUNT D " + quoted + " -label SOURCE");
	std::string dir, file = installerRelative;
	const size_t slash = installerRelative.find_last_of('/');
	if (slash != std::string::npos) {
		dir = installerRelative.substr(0, slash);
		file = installerRelative.substr(slash + 1);
	}
	for (char &c : dir)
		if (c == '/') c = '\\';
	launch.push_back("D:");
	launch.push_back("cd \\" + dir);
	launch.push_back(file);
	return true;
}

int preferredStartupProgram(const std::vector<std::string> &paths, const std::string &gameName)
{
	auto squash = [](const std::string &s) {
		std::string o;
		for (char c : fsutil::toLower(s)) if (c != ' ') o += c;
		return o;
	};
	const std::string want = squash(gameName);
	for (size_t i = 0; i < paths.size(); ++i)
		if (!want.empty() && squash(fsutil::stripExtension(fsutil::baseName(paths[i]))) == want)
			return (int)i;
	for (size_t i = 0; i < paths.size(); ++i)
		if (!isInstallerPath(paths[i]))
			return (int)i;
	return paths.empty() ? -1 : 0;
}

bool setDefaultLauncher(Gamebox &box, const std::string &relativePath, std::string *error)
{
	PlistValue entry = PlistValue::dict();
	entry.set("BXLauncherTitle", PlistValue::string("Launch " + fsutil::baseName(relativePath)));
	entry.set("BXLauncherPath", PlistValue::string(relativePath));
	entry.set("BXLauncherIsDefault", PlistValue::boolean(true));
	PlistValue list = PlistValue::array();
	if (const PlistValue *old = box.gameInfo().get("BXLaunchers"))
		for (const auto &item : old->items()) {
			const PlistValue *p = item.get("BXLauncherPath");
			if (p && p->str() == relativePath) continue;
			PlistValue copy = item;
			copy.remove("BXLauncherIsDefault");
			list.items().push_back(copy);
		}
	list.items().insert(list.items().begin(), entry);
	box.gameInfo().set("BXLaunchers", list);
	return box.saveGameInfo(error);
}

} // namespace boxer
