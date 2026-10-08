// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXImportSession.m, BXAppController+BXGamesFolder.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Import session state and source checks (BXImportSession.h:23-34 stages,
// +canImportFromSourceURL: IS:408, GF:398 _isReservedURL).
//
// AROS: only folders (and mounted volumes as folders) are accepted for now;
// disc images are not supported yet. Reserved sources are refused before
// anything is created, so a refused or cancelled source leaves no trace.
#pragma once

#include "installerscan.h"

#include <string>
#include <vector>

namespace boxer {

struct DataLocations;

enum class ImportStage {
	WaitingForSource,
	LoadingSource,
	WaitingForInstaller,
	RunningInstaller,
	ReadyToFinalize,
	ImportingSourceFiles,
	ChoosingStartupProgram,   // AROS loop: the startup program is chosen in the import window
	Finished,
	// later stages arrive with their loop increments
};

enum class SourceCheck {
	Ok,
	Missing,            // does not exist
	NotFolder,          // a file: images are not importable yet
	Gamebox,            // a .boxer, or inside one: open it instead
	BoxerData,          // the data directory or any "Boxer Data"
	GamesFolder,        // the games folder itself or a folder that contains it
	ContainsDataDir,    // would copy Boxer's own data into the game
};

// gamesFolder may be empty (then that rule is skipped). message gets a
// sentence for the requester, naming the source.
SourceCheck checkImportSource(const std::string &path, const DataLocations &locations,
                              const std::string &gamesFolder, std::string *message = nullptr);

// +gameboxNameForGameAtURL: (IP:343-404): "ULTIMA_8 (1994)" -> "Ultima 8".
std::string gameboxNameForSource(const std::string &sourceFolderName);
// +validGameboxNameFromName: leading dots removed, / \ : become '-'.
std::string validGameboxName(const std::string &name);
// ADBDefaultIncrementedFilenameFormat "%1$@ (%3$lu).%2$@": "Dune.boxer",
// "Dune (2).boxer", ... ; increment 1 means the plain name.
std::string incrementedGameboxName(const std::string &name, unsigned increment);

// The ready-to-import panel's text. The name stands on a line of its own,
// so a long name is not cut off together with the end of the sentence (a
// Zune Text object neither wraps nor shortens). A name longer than
// maxNameChars is shortened in the middle with "...", keeping its start and
// its end, where names usually differ ("... Part 2"); the window shows the
// full name as bubble help.
constexpr size_t kReadyNameChars = 40;
std::string readyToImportText(const std::string &name, size_t maxNameChars = kReadyNameChars);
// "...", put in the middle of text longer than maxChars (at least 5), at
// word breaks when one is near.
std::string shortenMiddle(const std::string &text, size_t maxChars);

struct ImportSession {
	ImportStage stage = ImportStage::WaitingForSource;
	std::string sourcePath;
	InstallerScanResult scan;
	// The one directory this session created; only it may ever be
	// deleted by cancel or failure. Empty until createGamebox succeeds.
	std::string createdGamebox;
	std::string createdIdentifier; // its BXGameIdentifier, checked before deleting
	std::string gamesFolder;     // where createdGamebox was made
	std::string installerPath;   // relative to the source, while installing

	// IS:importFromSourceURL: a refused source keeps the session waiting.
	SourceCheck chooseSource(const std::string &path, const DataLocations &locations,
	                         const std::string &gamesFolder, std::string *message = nullptr);
	// IS:660 cancelSourceSelection: back to the dropzone. Nothing on disk
	// belongs to the session yet, so nothing is deleted.
	void cancelSourceSelection();
	// installerScanDidFinish: (IS:169-230). On a scan error the session goes
	// back to waiting for a source; with installers to offer it waits for
	// the choice; otherwise the installer is skipped. Returns the error.
	InstallerScanError applyScan(const InstallerScanResult &result);
	// IS skipInstaller (only before anything was installed).
	void skipInstaller();

	// _generateGameboxWithError: (IS:1476-1552): a new, uniquely named
	// "<name>.boxer" in gamesFolder with an empty C.harddisk and a type-1
	// identifier. An existing directory of the same name is never reused
	// or touched: the next increment is tried. On failure nothing created
	// by this call remains.
	bool createGamebox(const std::string &gamesFolder, std::string *error = nullptr);
	std::string rootDrivePath() const;   // <gamebox>/C.harddisk
	// BXGamebox gameName: the gamebox file name without ".boxer".
	std::string gameName() const;
	// IS:1004-1037: where the source files go for a game that installed
	// nothing: C.harddisk/<DOS name> when the source needs a subfolder,
	// else C.harddisk itself. Creates the subfolder.
	bool prepareCopyDestination(std::string &destination, std::string *error = nullptr) const;
	// IS:1554 gameDidInstall: any regular file below C.harddisk.
	bool gameDidInstall() const;
	// IS:_cleanup: deletes createdGamebox and nothing else, and only while
	// it is still a gamebox directly inside gamesFolder. True when nothing
	// is left to delete.
	bool discardGamebox(std::string *error = nullptr);
};

// IS:1421-1474 reduced: the source folder as hard disk D (folder sources
// only; CD/floppy typing is not supported yet) and the commands that start the
// chosen installer on it. False if the source path cannot be quoted.
bool installerSessionCommands(const std::string &sourcePath, const std::string &installerRelative,
                              std::vector<std::string> &preflight, std::vector<std::string> &launch);

// Which program to preselect in the startup-program list (sorted by
// depth). Our rule, no original precedent: a program whose base name equals
// the game name (case-insensitive, spaces ignored); else the shallowest one
// that is not an installer; else the first. -1 for an empty list.
int preferredStartupProgram(const std::vector<std::string> &relativePaths, const std::string &gameName);

// BXLaunchers with one default entry for relativePath (relative to the
// gamebox, '/'-separated), titled "Launch <file name>"; replaces any
// existing default flag. Saved through Gamebox::saveGameInfo.
class Gamebox;
bool setDefaultLauncher(Gamebox &box, const std::string &relativePath, std::string *error = nullptr);

} // namespace boxer
