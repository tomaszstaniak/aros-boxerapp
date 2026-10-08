// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXImportSession.m setGameboxName:, validateGameboxName:),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "gameboxrename.h"
#include "fsutil.h"
#include "gamebox.h"
#include "importsource.h"

#include <algorithm>
#include <cstdio>

namespace boxer {

namespace {

RenameFaultHook faultHook;

bool injected(RenameStep s) { return faultHook && faultHook(s); }

std::string stemOf(const std::string &gameboxPath)
{
	// "Games/Dune.boxer" -> "Games/Dune"; only ".boxer" is a gamebox suffix
	// here (BXGamebox gameName strips nothing else).
	const std::string p = fsutil::trimTrailingSlash(gameboxPath);
	if (p.size() > 6 && fsutil::toLower(p.substr(p.size() - 6)) == ".boxer") return p.substr(0, p.size() - 6);
	return p;
}

// Moves from -> to without ever replacing an object: rename() replaces an
// existing file on POSIX hosts, and AROS DOS Rename refuses one, so the
// target is checked first. sameObject: to names from in another case.
bool move(const std::string &from, const std::string &to, bool sameObject, std::string *error)
{
	if (!sameObject && fsutil::exists(to)) {
		if (error) *error = to + " already exists";
		return false;
	}
	if (!sameObject) {
		if (std::rename(from.c_str(), to.c_str()) == 0) return true;
		if (error) *error = "could not rename " + from + " to " + fsutil::baseName(to);
		return false;
	}
	// IS:474-485: through a temporary name, back on failure.
	const std::string temp = from + "-renaming";
	if (fsutil::exists(temp)) {
		if (error) *error = temp + " already exists";
		return false;
	}
	if (std::rename(from.c_str(), temp.c_str()) != 0) {
		if (error) *error = "could not rename " + from;
		return false;
	}
	if (std::rename(temp.c_str(), to.c_str()) == 0) return true;
	std::rename(temp.c_str(), from.c_str());
	if (error) *error = "could not rename " + from + " to " + fsutil::baseName(to);
	return false;
}

bool isLoneIcon(const std::string &stem)
{
	return fsutil::isFile(stem + ".info");
}

std::string identifierOf(const std::string &gameboxPath)
{
	Gamebox box;
	return box.open(gameboxPath) ? box.identifier() : std::string();
}

} // namespace

void setRenameFaultHook(RenameFaultHook hook) { faultHook = std::move(hook); }

NameCheck checkGameboxRename(const std::string &gameboxPath, const std::string &requested,
                             std::string &sanitised, std::string *message)
{
	sanitised = validGameboxName(requested);
	const std::string stem = stemOf(gameboxPath);
	const std::string current = fsutil::baseName(stem);
	auto say = [&](NameCheck c, const std::string &m) { if (message) *message = m; return c; };
	if (sanitised.empty()) return say(NameCheck::Empty, "A game needs a name.");
	if (sanitised == current) return say(NameCheck::Unchanged, "");
	// AROS file names: SFS takes 107 characters, and "<name>.boxer" plus
	// the icon's "<name>.info" must both fit.
	if (sanitised.size() > 100) return say(NameCheck::TooLong, "The name \"" + sanitised + "\" is too long.");
	const bool caseOnly = fsutil::toLower(sanitised) == fsutil::toLower(current);
	const std::string newStem = fsutil::join(fsutil::parent(stem), sanitised);
	const bool taken = caseOnly ? fsutil::exists(newStem)
	                            : fsutil::exists(newStem + ".boxer") || fsutil::exists(newStem) ||
	                                  fsutil::exists(newStem + ".info");
	// The original's message (IS:554), straight quotes for Latin-1 fonts.
	if (taken) return say(NameCheck::Taken, "The name \"" + sanitised + "\" is already taken. Please choose another.");
	return say(NameCheck::Ok, "");
}

RenameOutcome renameGameboxPair(const std::string &gameboxPath, const std::string &newName,
                                const std::function<bool(const std::string &, std::string *)> &updateIcon)
{
	RenameOutcome out;
	out.gameboxPath = fsutil::trimTrailingSlash(gameboxPath);
	const std::string oldStem = stemOf(gameboxPath);
	const bool hasIcon = isLoneIcon(oldStem);
	out.iconStem = hasIcon ? oldStem : std::string();
	std::string name;
	const NameCheck check = checkGameboxRename(gameboxPath, newName, name, &out.message);
	if (check == NameCheck::Unchanged) { out.kind = RenameOutcome::Renamed; out.iconUpdated = true; return out; }
	if (check != NameCheck::Ok) return out;
	const bool caseOnly = fsutil::toLower(name) == fsutil::toLower(fsutil::baseName(oldStem));
	const std::string newStem = fsutil::join(fsutil::parent(oldStem), name);
	const std::string newBox = newStem + ".boxer";

	std::string err;
	if (injected(RenameStep::MoveGamebox) ? (err = "could not rename " + out.gameboxPath, false)
	                                      : move(out.gameboxPath, newBox, caseOnly, &err)) {
		out.gameboxPath = newBox;
	} else {
		out.message = "The game could not be renamed: " + err + ".";
		return out;
	}
	if (hasIcon) {
		if (injected(RenameStep::MoveIcon) ? (err = "could not rename " + oldStem + ".info", false)
		                                   : move(oldStem + ".info", newStem + ".info", caseOnly, &err)) {
			out.iconStem = newStem;
		} else {
			const std::string iconErr = err;
			const bool back = !injected(RenameStep::RestoreGamebox) &&
			                  move(newBox, oldStem + ".boxer", caseOnly, &err);
			if (back) {
				out.kind = RenameOutcome::RolledBack;
				out.gameboxPath = oldStem + ".boxer";
				out.message = "The game could not be renamed: its icon could not be renamed (" + iconErr +
				              "). Nothing was changed.";
			} else {
				out.kind = RenameOutcome::IconLeftBehind;
				out.message = "The game is now called \"" + name + "\", but its icon still has the old name \"" +
				              fsutil::baseName(oldStem) + "\" (" + iconErr + ").\n"
				              "Opening the game from that icon still finds it by its identifier; "
				              "Boxer then offers to rename the icon.";
			}
			return out;
		}
	}
	out.kind = RenameOutcome::Renamed;
	if (hasIcon && updateIcon) {
		err.clear();
		out.iconUpdated = !injected(RenameStep::UpdateIcon) && updateIcon(newStem, &err);
		if (!out.iconUpdated)
			out.message = "The game was renamed, but its icon could not be updated" +
			              (err.empty() ? std::string(".") : ": " + err + ".");
	} else {
		out.iconUpdated = true;
	}
	return out;
}

std::vector<std::string> gameboxesWithIdentifier(const std::string &folder, const std::string &identifier)
{
	std::vector<std::string> found, names;
	if (identifier.empty() || !fsutil::list(folder, names)) return found;
	std::sort(names.begin(), names.end());
	for (const auto &n : names) {
		const std::string p = fsutil::join(folder, n);
		if (fsutil::extension(n) != "boxer" || !fsutil::isDirectory(p)) continue;
		if (identifierOf(p) == identifier) found.push_back(p);
	}
	return found;
}

std::vector<SidecarCandidate> locateSidecarGamebox(const std::string &folder, const std::string &gameboxToolType,
                                                   const std::string &boxerId)
{
	std::vector<SidecarCandidate> out;
	// GAMEBOX is a file name beside the icon; anything with a path in it is
	// not ours to follow.
	if (!gameboxToolType.empty() && gameboxToolType.find('/') == std::string::npos &&
	    gameboxToolType.find(':') == std::string::npos) {
		const std::string p = fsutil::join(folder, gameboxToolType);
		if (fsutil::isDirectory(p)) {
			SidecarCandidate c;
			c.path = p;
			c.namedByIcon = true;
			c.identifierMatches = !boxerId.empty() && identifierOf(p) == boxerId;
			out.push_back(c);
		}
	}
	for (const auto &p : gameboxesWithIdentifier(folder, boxerId)) {
		auto it = std::find_if(out.begin(), out.end(), [&](const SidecarCandidate &c) {
			return fsutil::toLower(c.path) == fsutil::toLower(p);
		});
		if (it != out.end()) continue;
		SidecarCandidate c;
		c.path = p;
		c.identifierMatches = true;
		out.push_back(c);
	}
	return out;
}

bool sidecarLookupIsCertain(const std::vector<SidecarCandidate> &candidates)
{
	return candidates.size() == 1 && candidates[0].identifierMatches;
}

bool repairSidecarName(const std::string &iconStem, const std::string &gameboxPath,
                       const std::function<bool(const std::string &, std::string *)> &updateIcon,
                       std::string *error)
{
	const std::string target = stemOf(gameboxPath);
	if (fsutil::toLower(target) != fsutil::toLower(iconStem)) {
		if (fsutil::exists(target + ".info")) {
			if (error) *error = target + ".info already exists; the old icon was left as it is";
			return false;
		}
		if (!move(iconStem + ".info", target + ".info", false, error)) return false;
	}
	return !updateIcon || updateIcon(target, error);
}

} // namespace boxer
