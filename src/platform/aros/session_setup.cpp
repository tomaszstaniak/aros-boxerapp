// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXSession.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Moved here from core_smoke.cpp (unchanged logic) so that the Zune session
// window and the smoke host open gameboxes the same way.
#include "session_setup.h"
#include "../../model/datalocations.h"

#include <cstdio>

namespace boxer {

// Launcher path (relative to the gamebox) -> DOS path on its bundled drive.
// Case is kept; DOSBox's local drives match names case-insensitively.
bool dosPathForLauncher(const std::vector<BundledDrive> &drives, const std::string &gamebox,
                        const std::string &relative, std::string &dosPath)
{
	std::string full = gamebox;
	if (!full.empty() && full.back() != '/' && full.back() != ':')
		full += '/';
	full += relative;
	for (const auto &d : drives) {
		std::string root = d.sourcePath;
		if (!root.empty() && root.back() != '/' && root.back() != ':')
			root += '/';
		if (d.isImage || d.queued || full.compare(0, root.size(), root) != 0)
			continue;
		std::string rest = full.substr(root.size());
		for (auto &c : rest)
			if (c == '/') c = '\\';
		dosPath = std::string(1, d.letter) + ":\\" + rest;
		return true;
	}
	return false;
}

// BXSession's session setup for one gamebox, reduced to what the smoke run
// needs: identifier, data directory, shadow mappings, configuration
// layering, preflight mounts and the default launcher.
bool openGamebox(const SetupLog &log, const std::string &path, const std::string &dataDir,
                 const std::string &confDir, std::vector<ConfigFile> &confs,
                 std::vector<std::string> &preflight, std::vector<std::string> &launch,
                 ShadowFileSystem &shadow, std::string &error, std::string *gameName,
                 bool shadowWrites)
{
	char line[512];
	if (dataDir.empty() || confDir.empty()) {
		error = "GAMEBOX needs DATA and CONFDIR";
		return false;
	}
	Gamebox box;
	if (!box.open(path, &error))
		return false;
	bool persisted = false;
	const std::string id = box.ensureIdentifier(&persisted, &error);
	if (id.empty())
		return false;
	if (!persisted) {
		// A session-only identifier would orphan the saved state.
		error = "identifier could not be stored in the gamebox: " + error;
		return false;
	}
	std::string message;
	if (prepareDataDir(dataDir, &message) != DataDirStatus::Ready) {
		error = "data directory not usable: " + message;
		return false;
	}
	DataLocations where;
	where.dataDir = dataDir;
	const auto drives = box.bundledDrives();
	for (const auto &d : drives) {
		if (!shadowWrites) {
			snprintf(line, sizeof line, "drive %c: %s, writes go into the gamebox (import)", d.letter, d.sourcePath.c_str());
			log(line);
			continue;
		}
		const bool readOnly = d.type == DriveType::CDROM;
		shadow.addMapping(d.sourcePath, where.shadowRoot(id, d), readOnly);
		snprintf(line, sizeof line, "drive %c: %s -> shadow %s%s", d.letter, d.sourcePath.c_str(),
		         where.shadowRoot(id, d).c_str(), d.queued ? " (queued)" : "");
		log(line);
	}
	// Smoke overrides (OPTCONF) go after the gamebox conf, before Launch.conf.
	auto layered = sessionConfigFiles(confDir, &box);
	if (layered.empty()) {
		error = "no configuration files";
		return false;
	}
	layered.insert(layered.end() - 1, confs.begin(), confs.end());
	confs = layered;
	std::vector<std::string> skipped;
	for (const auto &c : mountCommands(drives, &skipped))
		preflight.push_back(c);
	for (const auto &s : skipped)
		log("drive not mountable: " + s);
	if (launch.empty()) {
		for (const auto &l : box.launchers()) {
			if (!l.isDefault && box.launchers().size() > 1)
				continue;
			std::string dosPath;
			if (!dosPathForLauncher(drives, box.path(), l.path, dosPath)) {
				error = "default launcher not on a mounted drive: " + l.path;
				return false;
			}
			const auto slash = dosPath.rfind('\\');
			launch.push_back(dosPath.substr(0, 2));
			launch.push_back("cd " + dosPath.substr(2, slash > 2 ? slash - 2 : 1));
			launch.push_back(dosPath.substr(slash + 1) + (l.arguments.empty() ? "" : " " + l.arguments));
			break;
		}
	}
	snprintf(line, sizeof line, "gamebox %s id %s, %u config files, %u mounts, %u launch commands",
	         box.gameName().c_str(), id.c_str(), unsigned(confs.size()),
	         unsigned(preflight.size()), unsigned(launch.size()));
	log(line);
	if (gameName)
		*gameName = box.gameName();
	return true;
}

} // namespace boxer
