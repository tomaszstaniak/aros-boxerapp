// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXFileTypes.m, BXSession.m,
// BXSession+BXFileManagement.m), https://github.com/alunbestor/Boxer at
// commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// What the launch panel needs from the model: the executable scan of a
// bundled drive (BXSession executableScanForDrive: with BXFileTypes
// isCompatibleExecutableAtPath:) and the per-game settings that hold the
// recent programs and the launch-panel flags (BXSession gameSettings,
// "BXGameSettings: <identifier>" in the original's user defaults).
#pragma once

#include "plist.h"

#include <string>
#include <vector>

namespace boxer {

class FileSystem;
struct DataLocations;

enum class ExecutableType { Unknown, DOS, Windows, OS2 };

// +[BXFileTypes typeOfExecutableInStream:error:] on the file at path, read
// through fs (so a shadowed copy is what is examined).
ExecutableType executableType(FileSystem &fs, const std::string &path);

// +[BXFileTypes isCompatibleExecutableAtPath:filesystem:error:]: .COM and
// .BAT always; .EXE only if its header says DOS.
bool isCompatibleExecutable(FileSystem &fs, const std::string &path);

// The scan block of executableScanForDrive:, depth first from root:
// hidden entries skipped (dot files and what fs.showFile() refuses, the
// original's NSDirectoryEnumerationSkipsHiddenFiles plus
// shouldShowFileWithName:), nested mountable folders not entered, every
// compatible executable returned as a host path. The original kept the
// enumerator's order (alphabetical on HFS+); entries are sorted here
// case-insensitively per directory to give the same order on any volume.
std::vector<std::string> scanExecutables(FileSystem &fs, const std::string &root);

// BXGameRecentPrograms entry: "path" relative to the gamebox when inside it,
// else absolute; "arguments" only when non-empty (BXSession _saveGameSettings).
struct RecentProgram {
	std::string path;
	std::string arguments;
};

// Per-game settings. Where the original used the user defaults key
// "BXGameSettings: <identifier>", the port keeps one plist per gamebox in
// the data directory (decision D2: persistent data lives there):
// <data>/Game Settings/<identifier>.plist. Unknown keys are kept.
class GameSettings {
public:
	static std::string pathFor(const DataLocations &where, const std::string &identifier);

	// A missing file is an empty dictionary, not an error.
	bool load(const std::string &path, std::string *error = nullptr);
	bool save(std::string *error = nullptr) const;

	const std::string &path() const { return path_; }
	PlistValue &dict() { return dict_; }
	const PlistValue &dict() const { return dict_; }

	bool flag(const std::string &key) const;
	void setFlag(const std::string &key, bool value);

	std::vector<RecentProgram> recentPrograms() const;
	void setRecentPrograms(const std::vector<RecentProgram> &programs);

private:
	std::string path_;
	PlistValue dict_ = PlistValue::dict();
};

// Settings keys of BXSession.m.
extern const char *const kRecentProgramsKey;       // BXGameRecentPrograms
extern const char *const kShowLaunchPanelKey;      // showLaunchPanel (one-shot)
extern const char *const kAlwaysShowLaunchPanelKey; // alwaysShowLaunchPanel

// BXRecentProgramsLimit.
constexpr size_t kRecentProgramsLimit = 10;

// -[BXSession noteRecentProgram:]: an existing record with the same path and
// arguments is removed, the program goes to the front, the list is cut to
// the limit. Paths compare case-insensitively (AROS and HFS+ volumes).
void noteRecentProgram(std::vector<RecentProgram> &list, const RecentProgram &program);
// -[BXSession removeRecentProgram:].
void removeRecentProgram(std::vector<RecentProgram> &list, const RecentProgram &program);
bool samePath(const std::string &a, const std::string &b);

} // namespace boxer
